// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_COMPACT_BLOCKS_HPP
#define KTH_NODE_COMPACT_BLOCKS_HPP

#include <cstdint>
#include <vector>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/node/define.hpp>

namespace kth::blockchain {
struct block_chain;
}

namespace kth::node {

// What reconstructing one compact block against transactions the node already
// holds came to. `collision` and `rejected` both mean "ask for the full
// block": a collision is ambiguous, and a rejected body does not match the
// header it claims. Neither is a reason to ban the peer.
enum class compact_reconstruct {
    complete,
    missing,
    collision,
    rejected
};

struct compact_reconstruction {
    compact_reconstruct status{compact_reconstruct::rejected};
    // Filled where a transaction is known. Holes are null transactions, and
    // `missing` lists those absolute indexes in block order. Empty unless
    // `status` is `complete` or `missing`.
    domain::chain::transaction::list transactions;
    std::vector<uint64_t> missing;
};

// Prefilled transactions are placed at their absolute indexes. Each short id,
// in order, fills the next hole from `available`. Two available transactions
// for one short id is a collision, and that block is not partially used.
// `available` is whatever the node already has (the mempool); it is not asked
// to be complete.
[[nodiscard]] KND_API
compact_reconstruction reconstruct_compact_block(
    domain::message::compact_block const& message,
    std::vector<domain::chain::transaction> const& available);

[[nodiscard]] KND_API
compact_reconstruction reconstruct_compact_block(
    domain::message::compact_block const& message, blockchain::block_chain const& chain);

// Place `provided` into the holes of a `missing` reconstruction, in order.
// The result is `complete` only when the count matches, each transaction has
// the short id that hole was assigned, and the merkle root commits to them.
[[nodiscard]] KND_API
compact_reconstruction accept_block_transactions(
    domain::message::compact_block const& message,
    compact_reconstruction const& partial,
    domain::chain::transaction::list const& provided);

} // namespace kth::node

#endif
