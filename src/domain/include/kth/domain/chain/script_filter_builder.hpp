// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_SCRIPT_FILTER_BUILDER_HPP
#define KTH_DOMAIN_CHAIN_SCRIPT_FILTER_BUILDER_HPP

#include <expected>
#include <span>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/golomb_set.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

/// Why building or parsing a script filter was refused.
enum class script_filter_error {
    /// The spent outputs are not one for each input that spends a previous output.
    spent_outputs,
    /// The served bytes do not contain a 32-byte root.
    too_short,
    /// The Golomb set could not be built or parsed.
    set,
};

/// SHA-256 of a bare locking script.
/// The caller decides whether the script is watched; this hashes every script, including an empty one.
[[nodiscard]]
KD_API
hash_digest script_key(byte_span script);

/// SHA-256 of 0x01 followed by the category.
/// The tag keeps a category key from equalling the key of any locking script.
[[nodiscard]]
KD_API
hash_digest category_key(hash_digest const& category);

/// Sorted leaves of a block.
/// `spent` holds the output each input spends, for every transaction after the first, in block order.
/// A locking script that is empty or starts with OP_RETURN has no script leaf. Its token category still has a leaf.
[[nodiscard]]
KD_API
std::expected<std::vector<script_leaf>, script_filter_error>
script_leaves(block const& blk, std::span<output const> spent);

/// A script filter as it is served: the Golomb set of the distinct leaf keys, and the root of those leaves.
struct KD_API script_filter {
    golomb_set set;
    hash_digest root;

    friend bool operator==(script_filter const&, script_filter const&) = default;
};

/// The set of the distinct leaf keys, keyed by the block hash, and the root of the leaves.
[[nodiscard]]
KD_API
std::expected<script_filter, script_filter_error>
build_script_filter(block const& blk, std::span<output const> spent);

/// Served bytes: the root, then the set.
[[nodiscard]]
KD_API
data_chunk to_data(script_filter const& filter);

/// Splits served bytes into the set and the root.
/// A buffer shorter than a root is refused before the set is parsed.
[[nodiscard]]
KD_API
std::expected<script_filter, script_filter_error>
parse_script_filter(byte_span data);

/// Double SHA-256 of the served bytes.
[[nodiscard]]
KD_API
hash_digest script_filter_hash(script_filter const& filter);

/// Double SHA-256 of the filter hash followed by the previous header.
/// The header chain therefore commits to the root as well as to the set.
[[nodiscard]]
KD_API
hash_digest script_filter_header(script_filter const& filter, hash_digest const& previous_header);

/// The set of the distinct keys of `txs`, keyed by `hash`.
/// No transaction is a coinbase, so every input spends one output in `spent`, in order.
[[nodiscard]]
KD_API
std::expected<golomb_set, script_filter_error>
build_tx_script_filter(hash_digest const& hash, std::span<transaction const> txs, std::span<output const> spent);

} // namespace kth::domain::chain

#endif
