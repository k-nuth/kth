// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_filter_headers.hpp>

#include <cstring>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

expect<compact_filter_headers> compact_filter_headers::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const stop = read_hash(reader);
    if ( ! stop) return std::unexpected(stop.error());
    auto const previous = read_hash(reader);
    if ( ! previous) return std::unexpected(previous.error());
    auto const count = reader.read_size_little_endian();
    if ( ! count) return std::unexpected(count.error());
    // Refused on the count alone.
    if (*count > max_compact_filter_headers) return std::unexpected(error::invalid_size);

    compact_filter_headers result;
    result.filter_type = *type;
    result.stop_hash = *stop;
    result.previous_filter_header = *previous;
    // The hashes are taken as one run of bytes, so nothing is allocated for
    // a count the message does not carry.
    auto const hashes = reader.read_bytes(*count * hash_size);
    if ( ! hashes) return std::unexpected(hashes.error());
    result.filter_hashes.resize(*count);
    if (*count != 0) {
        std::memcpy(result.filter_hashes.data(), hashes->data(), hashes->size());
    }
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return result;
}

expect<void> compact_filter_headers::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(stop_hash); ! r) return r;
    if (auto r = writer.write_hash(previous_filter_header); ! r) return r;
    if (auto r = writer.write_variable_little_endian(filter_hashes.size()); ! r) return r;
    for (auto const& hash : filter_hashes) {
        if (auto r = writer.write_hash(hash); ! r) return r;
    }
    return {};
}

size_t compact_filter_headers::serialized_size(uint32_t /*version*/) const {
    return 1 + hash_size + hash_size + infrastructure::message::variable_uint_size(filter_hashes.size())
        + filter_hashes.size() * hash_size;
}

} // namespace kth::domain::message
