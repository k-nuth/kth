// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_compact_filter_proof.hpp>

#include <kth/domain/message/version.hpp>

namespace kth::domain::message {

expect<get_compact_filter_proof> get_compact_filter_proof::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    auto const index = reader.read_little_endian<uint32_t>();
    if ( ! index) return std::unexpected(index.error());
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return get_compact_filter_proof{*type, *block, *index};
}

expect<void> get_compact_filter_proof::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(block_hash); ! r) return r;
    return writer.write_little_endian<uint32_t>(leaf_index);
}

size_t get_compact_filter_proof::serialized_size(uint32_t /*version*/) const {
    return 1 + hash_size + sizeof(uint32_t);
}

} // namespace kth::domain::message
