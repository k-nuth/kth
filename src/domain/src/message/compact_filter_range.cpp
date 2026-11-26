// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_filter_range.hpp>

#include <algorithm>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

namespace {

// Whether a run of `held` leaves from `first`, in a tree of `count` leaves,
// has a leaf before it and a leaf after it.
struct neighbours {
    bool before;
    bool after;
};

constexpr neighbours neighbours_of(uint32_t count, uint32_t first, size_t held) noexcept {
    return {first > 0, uint64_t(first) + uint64_t(held) < uint64_t(count)};
}

constexpr bool fits(uint32_t count, uint32_t first, size_t held) noexcept {
    return held <= max_compact_filter_range_leaves && uint64_t(first) + uint64_t(held) <= uint64_t(count);
}

} // namespace

expect<compact_filter_range> compact_filter_range::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    auto const count = reader.read_little_endian<uint32_t>();
    if ( ! count) return std::unexpected(count.error());
    auto const first = reader.read_little_endian<uint32_t>();
    if ( ! first) return std::unexpected(first.error());
    auto const held = reader.read_size_little_endian();
    if ( ! held) return std::unexpected(held.error());
    // Refused before anything is reserved for it: the leaves fit the tree and
    // a message, and each one is in what is left to read.
    if ( ! fits(*count, *first, *held) || *held > reader.remaining_size() / chain::script_leaf_size) {
        return std::unexpected(error::invalid_size);
    }

    compact_filter_range result;
    result.filter_type = *type;
    result.block_hash = *block;
    result.leaf_count = *count;
    result.first_index = *first;
    result.leaves.reserve(*held);
    for (size_t i = 0; i < *held; ++i) {
        auto const leaf = detail::read_tree_leaf(reader);
        if ( ! leaf) return std::unexpected(leaf.error());
        result.leaves.push_back(*leaf);
    }
    auto const [before, after] = neighbours_of(*count, *first, *held);
    if (before) {
        auto const leaf = detail::read_tree_leaf(reader);
        if ( ! leaf) return std::unexpected(leaf.error());
        result.before = *leaf;
    }
    if (after) {
        auto const leaf = detail::read_tree_leaf(reader);
        if ( ! leaf) return std::unexpected(leaf.error());
        result.after = *leaf;
    }
    auto hashes = detail::read_tree_hashes(reader, 2 * max_compact_filter_tree_depth);
    if ( ! hashes) return std::unexpected(hashes.error());
    result.hashes = std::move(*hashes);
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return result;
}

expect<void> compact_filter_range::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    auto const [has_before, has_after] = neighbours_of(leaf_count, first_index, leaves.size());
    if ( ! fits(leaf_count, first_index, leaves.size())
        || has_before != before.has_value() || has_after != after.has_value()) {
        return std::unexpected(error::invalid_size);
    }
    if (auto r = writer.write_byte(filter_type); ! r) return r;
    if (auto r = writer.write_hash(block_hash); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(leaf_count); ! r) return r;
    if (auto r = writer.write_little_endian<uint32_t>(first_index); ! r) return r;
    if (auto r = writer.write_variable_little_endian(leaves.size()); ! r) return r;
    for (auto const& leaf : leaves) {
        if (auto r = detail::write_tree_leaf(writer, leaf); ! r) return r;
    }
    if (before) {
        if (auto r = detail::write_tree_leaf(writer, *before); ! r) return r;
    }
    if (after) {
        if (auto r = detail::write_tree_leaf(writer, *after); ! r) return r;
    }
    return detail::write_tree_hashes(writer, hashes, 2 * max_compact_filter_tree_depth);
}

size_t compact_filter_range::serialized_size(uint32_t /*version*/) const {
    return 1 + hash_size + sizeof(uint32_t) + sizeof(uint32_t)
        + infrastructure::message::variable_uint_size(leaves.size())
        + chain::script_leaf_size * (leaves.size() + (before ? 1 : 0) + (after ? 1 : 0))
        + detail::tree_hashes_size(hashes);
}

} // namespace kth::domain::message
