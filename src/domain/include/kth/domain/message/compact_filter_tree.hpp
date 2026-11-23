// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_FILTER_TREE_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_FILTER_TREE_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// The script filter: the scripts and token categories of the outputs a block
// creates and of the ones it spends. It is served with the root of a Merkle
// tree over those entries, its leaves, in front of it. `getcfrange` and
// `getcfproof` ask what that tree holds, so a wallet can tell which of two
// nodes that disagree serves a wrong filter without downloading the block.
inline constexpr uint8_t script_compact_filter = 1;

// The most levels above the leaves of a tree of up to 2^32 leaves: the most
// hashes in the branch of one leaf, or of one transaction in a block. A range
// has at most two for each level.
inline constexpr size_t max_compact_filter_tree_depth = 32;

// The longest key prefix a `getcfrange` asks for: a whole key.
inline constexpr uint16_t max_compact_filter_prefix_bits = 256;

// The most leaves one `cfrange` holds: what fits a message.
inline constexpr size_t max_compact_filter_range_leaves = max_payload_size / chain::script_leaf_size;

namespace detail {

// A count of hashes, at most `limit`, then the hashes.
inline
expect<std::vector<hash_digest>> read_tree_hashes(byte_reader& reader, size_t limit) {
    auto const count = reader.read_size_little_endian();
    if ( ! count) return std::unexpected(count.error());
    // Refused before anything is reserved for it.
    if (*count > limit) return std::unexpected(error::invalid_size);
    std::vector<hash_digest> hashes;
    hashes.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const hash = read_hash(reader);
        if ( ! hash) return std::unexpected(hash.error());
        hashes.push_back(*hash);
    }
    return hashes;
}

inline
expect<void> write_tree_hashes(byte_writer& writer, std::vector<hash_digest> const& hashes, size_t limit) noexcept {
    if (hashes.size() > limit) return std::unexpected(error::invalid_size);
    if (auto r = writer.write_variable_little_endian(hashes.size()); ! r) return r;
    for (auto const& hash : hashes) {
        if (auto r = writer.write_hash(hash); ! r) return r;
    }
    return {};
}

inline
size_t tree_hashes_size(std::vector<hash_digest> const& hashes) {
    return infrastructure::message::variable_uint_size(hashes.size()) + hashes.size() * hash_size;
}

// One leaf: 41 bytes, and a kind the tree knows.
inline
expect<chain::script_leaf> read_tree_leaf(byte_reader& reader) {
    auto const bytes = reader.read_bytes(chain::script_leaf_size);
    if ( ! bytes) return std::unexpected(bytes.error());
    auto const leaf = chain::parse_script_leaf(*bytes);
    if ( ! leaf) return std::unexpected(error::illegal_value);
    return *leaf;
}

inline
expect<void> write_tree_leaf(byte_writer& writer, chain::script_leaf const& leaf) noexcept {
    // A kind the tree does not know is not read, so it is not written.
    if (uint8_t(leaf.kind) > uint8_t(chain::script_leaf_kind::transaction)) {
        return std::unexpected(error::illegal_value);
    }
    return writer.write_array(chain::to_data(leaf));
}

// Whether a leaf is the script or the token category of an output its block
// spends. The `cfproof` of such a leaf carries the transaction of that output.
[[nodiscard]]
constexpr bool is_spent_leaf(chain::script_leaf const& leaf) noexcept {
    return leaf.kind == chain::script_leaf_kind::spent || leaf.kind == chain::script_leaf_kind::spent_category;
}

} // namespace detail
} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_COMPACT_FILTER_TREE_HPP
