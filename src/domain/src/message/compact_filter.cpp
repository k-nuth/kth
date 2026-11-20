// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_filter.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

expect<compact_filter> compact_filter::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    auto const size = reader.read_size_little_endian();
    if ( ! size) return std::unexpected(size.error());
    if (*size > max_compact_filter_size) return std::unexpected(error::invalid_size);
    auto const bytes = reader.read_bytes(*size);
    if ( ! bytes) return std::unexpected(bytes.error());
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return compact_filter{*type, *block, data_chunk(bytes->begin(), bytes->end())};
}

expect<void> compact_filter::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(block_hash); ! r) return r;
    if (auto r = writer.write_variable_little_endian(filter.size()); ! r) return r;
    return writer.write_bytes(filter);
}

size_t compact_filter::serialized_size(uint32_t /*version*/) const {
    return 1 + hash_size + infrastructure::message::variable_uint_size(filter.size()) + filter.size();
}

} // namespace kth::domain::message
