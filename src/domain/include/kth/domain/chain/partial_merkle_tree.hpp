// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_PARTIAL_MERKLE_TREE_HPP
#define KTH_DOMAIN_CHAIN_PARTIAL_MERKLE_TREE_HPP

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// The part of a block's merkle tree that proves some of its transactions are
// in the block: what a `merkleblock` carries (BIP37). The tree is walked
// depth first; each node visited adds a flag bit, and a node whose subtree
// holds no match, or a leaf, adds its hash.
struct KD_API partial_merkle_tree {
    // The number of transactions in the block.
    uint32_t total_transactions{0};
    hash_list hashes;
    // The flag bits, eight to a byte, the first in the lowest bit.
    data_chunk flags;

    [[nodiscard]]
    friend bool operator==(partial_merkle_tree const&, partial_merkle_tree const&) = default;

    // From the hash of every transaction of a block, in block order, and
    // whether each is a match. Both lists have one entry per transaction, and
    // at least one.
    [[nodiscard]]
    static partial_merkle_tree build(hash_list const& txids, std::vector<bool> const& matches);

    struct extraction {
        // The merkle root the hashes lead to: the receiver compares it with
        // the block header's.
        hash_digest root{null_hash};
        // The matched transactions, with their place in the block.
        std::vector<std::pair<uint32_t, hash_digest>> matches;

        [[nodiscard]]
        friend bool operator==(extraction const&, extraction const&) = default;
    };

    // What the tree proves, or nothing when it is not a well-formed tree: no
    // transactions, more hashes than transactions, fewer flag bits than
    // hashes, hashes or flag bytes left over, or two equal children, which is
    // how a transaction list can be made to look like another one.
    [[nodiscard]]
    std::optional<extraction> extract() const;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_PARTIAL_MERKLE_TREE_HPP
