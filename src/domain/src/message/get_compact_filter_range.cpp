// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_compact_filter_range.hpp>

#include <kth/domain/message/version.hpp>

namespace kth::domain::message {

std::string const get_compact_filter_range::command = "getcfrange";
uint32_t const get_compact_filter_range::version_minimum = version::level::minimum;
uint32_t const get_compact_filter_range::version_maximum = version::level::maximum;

expect<get_compact_filter_range> get_compact_filter_range::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    auto const bits = reader.read_little_endian<uint16_t>();
    if ( ! bits) return std::unexpected(bits.error());
    // A prefix is never longer than a key.
    if (*bits > max_compact_filter_prefix_bits) return std::unexpected(error::invalid_size);
    auto const prefix = read_hash(reader);
    if ( ! prefix) return std::unexpected(prefix.error());
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return get_compact_filter_range{*type, *block, *bits, *prefix};
}

expect<void> get_compact_filter_range::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (prefix_bits > max_compact_filter_prefix_bits) return std::unexpected(error::invalid_size);
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(block_hash); ! r) return r;
    if (auto r = writer.write_little_endian<uint16_t>(prefix_bits); ! r) return r;
    return writer.write_hash(prefix);
}

size_t get_compact_filter_range::serialized_size(uint32_t /*version*/) const {
    return 1 + hash_size + sizeof(uint16_t) + hash_size;
}

} // namespace kth::domain::message
