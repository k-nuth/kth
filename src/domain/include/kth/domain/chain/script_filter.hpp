// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_SCRIPT_FILTER_HPP
#define KTH_DOMAIN_CHAIN_SCRIPT_FILTER_HPP

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
namespace kth::domain::chain {

/// Serialized size of one entry-tree leaf: a 32-byte key, the transaction
/// index, the input or output index, and the kind.
inline constexpr size_t script_leaf_size = 41;

static_assert(script_leaf_size == hash_size + sizeof(uint32_t) + sizeof(uint32_t) + 1);
static_assert(std::same_as<hash_digest, std::array<uint8_t, hash_size>>);

/// What a leaf's key comes from.
enum class script_leaf_kind : uint8_t {
    /// Locking script of an output the block creates.
    output = 0,
    /// Locking script of an output the block spends.
    spent = 1,
    /// Token category of an output the block creates.
    output_category = 2,
    /// Token category of an output the block spends.
    spent_category = 3,
};

/// One occurrence of a key in a block, and where that occurrence sits.
/// Member order is the tree order: key, transaction index, input or output
/// index, then kind. A key is an array of bytes, so keys compare from the
/// first byte toward the last.
struct KD_API script_leaf {
    hash_digest key;
    uint32_t tx_index;
    uint32_t index;
    script_leaf_kind kind;

    friend auto operator<=>(script_leaf const&, script_leaf const&) = default;
    friend bool operator==(script_leaf const&, script_leaf const&) = default;
};

/// Why a leaf, a prefix, or a range proof was refused.
enum class script_range_error {
    /// A serialized leaf is not 41 bytes, or its kind is not 0 to 3.
    leaf,
    /// A key prefix is longer than 256 bits, or a bit past its length is set.
    prefix,
    /// The leaves, their neighbours, and the edge hashes do not fit a tree of the stated size.
    range,
    /// The leaves rebuild the root, but they are out of order or do not bound the prefix.
    leaves,
    /// The leaves and hashes rebuild a root other than the one given.
    root,
};

/// Serializes a leaf: the key, then both indexes as little-endian uint32, then the kind.
[[nodiscard]]
KD_API
std::array<uint8_t, script_leaf_size> to_data(script_leaf const& leaf);

/// Parses one serialized leaf. A buffer of any other length, or a kind above 3, is refused.
[[nodiscard]]
KD_API
std::expected<script_leaf, script_range_error> parse_script_leaf(byte_span data);

/// Leaf hash: one SHA-256 of 0x00 followed by the serialized leaf.
/// The leading byte keeps a leaf hash from colliding with an inner-node hash.
[[nodiscard]]
KD_API
hash_digest script_leaf_hash(script_leaf const& leaf);

/// Inner-node hash: one SHA-256 of 0x01 followed by the left child and then the right child.
/// The leading byte keeps an inner-node hash from colliding with a leaf hash.
[[nodiscard]]
KD_API
hash_digest script_node_hash(hash_digest const& left, hash_digest const& right);

/// Root of the entry tree over leaves in tree order.
/// A node with no partner moves up unchanged, so a node is never paired with itself.
/// No leaves produce 32 zero bytes.
[[nodiscard]]
KD_API
hash_digest script_leaves_root(std::span<script_leaf const> leaves);

/// A half-open run of leaves, [first, end).
struct script_leaf_run {
    size_t first;
    size_t end;
};

/// Run of sorted leaves whose keys start with the first `bits` of `prefix`.
/// Zero bits selects every leaf, and 256 bits selects one exact key.
/// When no leaf matches, first equals end at the index where a match would be inserted.
/// A prefix longer than 256 bits, or with a bit set past its length, is refused,
/// so each prefix has one encoding.
[[nodiscard]]
KD_API
std::expected<script_leaf_run, script_range_error>
script_leaves_with_prefix(std::span<script_leaf const> leaves, hash_digest const& prefix, uint16_t bits);

/// Edge hashes that, with the run leaves[lo, end), rebuild the root.
/// From the leaves upward, each level contributes the node just left of the
/// run and then the node just right of it, where the run does not already
/// cover that side. A last node with no partner moves up unchanged and adds none.
/// An empty run, a reversed run, or a run past the last leaf is refused.
[[nodiscard]]
KD_API
std::expected<std::vector<hash_digest>, script_range_error>
script_range_hashes(std::span<script_leaf const> leaves, size_t lo, size_t end);

/// Rebuilds the root of a tree of `count` leaves from the consecutive run that
/// starts at `lo` and from the edge hashes of that run.
/// Too few hashes, too many hashes, an empty run, or a run past `count` is refused.
[[nodiscard]]
KD_API
std::expected<hash_digest, script_range_error>
script_range_root(uint32_t count, uint32_t lo, std::span<script_leaf const> run, std::span<hash_digest const> hashes);

/// Checks one prefix answer against the root stored in front of a script filter.
/// The tree has `count` leaves. `run` is the run that starts at `first` and
/// whose keys start with `prefix`. `before` is present exactly when `first`
/// is above zero, and `after` exactly when the run stops before the last leaf.
/// On success the tree holds no other leaf with that prefix.
/// A rebuilt root that differs from `root` is a different tree. Leaves that
/// do rebuild `root`, but are out of order or do not bound the prefix, are
/// the tree's own leaves presented wrongly.
[[nodiscard]]
KD_API
std::expected<void, script_range_error>
check_script_range(hash_digest const& root, uint32_t count, uint32_t first,
                   std::span<script_leaf const> run,
                   script_leaf const* before, script_leaf const* after,
                   std::span<hash_digest const> hashes,
                   hash_digest const& prefix, uint16_t bits);

} // namespace kth::domain::chain

#endif
