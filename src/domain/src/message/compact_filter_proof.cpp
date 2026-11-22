// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_filter_proof.hpp>

#include <utility>

#include <kth/domain/message/version.hpp>

namespace kth::domain::message {

expect<compact_filter_proof> compact_filter_proof::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    auto const count = reader.read_little_endian<uint32_t>();
    if ( ! count) return std::unexpected(count.error());
    auto const index = reader.read_little_endian<uint32_t>();
    if ( ! index) return std::unexpected(index.error());
    if (*index >= *count) return std::unexpected(error::invalid_size);
    auto const leaf = detail::read_tree_leaf(reader);
    if ( ! leaf) return std::unexpected(leaf.error());

    compact_filter_proof result;
    result.filter_type = *type;
    result.block_hash = *block;
    result.leaf_count = *count;
    result.leaf_index = *index;
    result.leaf = *leaf;

    auto leaf_branch = detail::read_tree_hashes(reader, max_compact_filter_tree_depth);
    if ( ! leaf_branch) return std::unexpected(leaf_branch.error());
    result.leaf_branch = std::move(*leaf_branch);

    auto tx = chain::transaction::from_data(reader, true);
    if ( ! tx) return std::unexpected(tx.error());
    result.transaction = std::move(*tx);
    auto tx_branch = detail::read_tree_hashes(reader, max_compact_filter_tree_depth);
    if ( ! tx_branch) return std::unexpected(tx_branch.error());
    result.transaction_branch = std::move(*tx_branch);

    if (detail::is_spent_leaf(result.leaf)) {
        compact_filter_proof_origin origin;
        auto const origin_block = read_hash(reader);
        if ( ! origin_block) return std::unexpected(origin_block.error());
        origin.block_hash = *origin_block;
        auto const origin_index = reader.read_little_endian<uint32_t>();
        if ( ! origin_index) return std::unexpected(origin_index.error());
        origin.transaction_index = *origin_index;
        auto origin_tx = chain::transaction::from_data(reader, true);
        if ( ! origin_tx) return std::unexpected(origin_tx.error());
        origin.transaction = std::move(*origin_tx);
        auto origin_branch = detail::read_tree_hashes(reader, max_compact_filter_tree_depth);
        if ( ! origin_branch) return std::unexpected(origin_branch.error());
        origin.transaction_branch = std::move(*origin_branch);
        result.origin = std::move(origin);
    }
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return result;
}

expect<void> compact_filter_proof::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (leaf_index >= leaf_count || detail::is_spent_leaf(leaf) != origin.has_value()) {
        return std::unexpected(error::invalid_size);
    }
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(block_hash); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(leaf_count); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(leaf_index); ! r) return r;
    if (auto r = detail::write_tree_leaf(writer, leaf); ! r) return r;
    if (auto r = detail::write_tree_hashes(writer, leaf_branch, max_compact_filter_tree_depth); ! r) return r;
    if (auto r = transaction.to_data(writer, true); ! r) return r;
    if (auto r = detail::write_tree_hashes(writer, transaction_branch, max_compact_filter_tree_depth); ! r) return r;
    if ( ! origin) return {};
    if (auto r = writer.write_hash(origin->block_hash); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(origin->transaction_index); ! r) return r;
    if (auto r = origin->transaction.to_data(writer, true); ! r) return r;
    return detail::write_tree_hashes(writer, origin->transaction_branch, max_compact_filter_tree_depth);
}

size_t compact_filter_proof::serialized_size(uint32_t /*version*/) const {
    auto size = 1 + hash_size + sizeof(uint32_t) + sizeof(uint32_t) + chain::script_leaf_size
        + detail::tree_hashes_size(leaf_branch) + transaction.serialized_size(true)
        + detail::tree_hashes_size(transaction_branch);
    if (origin) {
        size += hash_size + sizeof(uint32_t) + origin->transaction.serialized_size(true)
            + detail::tree_hashes_size(origin->transaction_branch);
    }
    return size;
}

} // namespace kth::domain::message
