// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_compact_filters.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

std::string const get_compact_filters::command = "getcfilters";
uint32_t const get_compact_filters::version_minimum = version::level::minimum;
uint32_t const get_compact_filters::version_maximum = version::level::maximum;

expect<get_compact_filters> get_compact_filters::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const start = reader.read_little_endian<uint32_t>();
    if ( ! start) return std::unexpected(start.error());
    auto const stop = read_hash(reader);
    if ( ! stop) return std::unexpected(stop.error());
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return get_compact_filters{*type, *start, *stop};
}

expect<void> get_compact_filters::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(start_height); ! r) return r;
    return writer.write_hash(stop_hash);
}

size_t get_compact_filters::serialized_size(uint32_t /*version*/) const {
    return 1 + sizeof(uint32_t) + hash_size;
}

} // namespace kth::domain::message
