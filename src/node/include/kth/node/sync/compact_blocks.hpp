// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_COMPACT_BLOCKS_HPP
#define KTH_NODE_SYNC_COMPACT_BLOCKS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/block_transactions.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node {
class p2p_node;
} // namespace kth::node

namespace kth::node::sync {

// Receiving compact blocks (BIP152, version 1: Bitcoin Cash has no witness).
//
// Once the node is caught up, a cmpctblock for the block after the connected
// tip is rebuilt from the transactions it carries and the mempool, and handed
// to block_chain::submit_block, the way a block found locally is: validation
// and connection are the pipeline's. Transactions the mempool lacks are asked
// of the peer with getblocktxn. What cannot be rebuilt (an ambiguous short ID,
// a wrong or late blocktxn, a body its header does not commit to, a refused
// submission) goes back to the header ingest, and the block is downloaded the
// ordinary way; none of it is held against the peer. During initial sync
// nothing is taken, and a cmpctblock contributes its header as before.

/// What rebuilding one compact block from the transactions at hand came to.
/// `collision` and `rejected` both mean the whole block is needed: a collision
/// is ambiguous, and a rejected body is not the one the header commits to.
enum class compact_reconstruct {
    complete,
    missing,
    collision,
    rejected
};

struct compact_reconstruction {
    compact_reconstruct status{compact_reconstruct::rejected};
    /// The block's transactions where they are known, null where they are
    /// not; `missing` lists those positions in block order. Empty unless
    /// `status` is `complete` or `missing`.
    domain::chain::transaction::list transactions;
    std::vector<uint64_t> missing;
};

/// Rebuild `message` from `available`, which need not hold every
/// transaction. Prefilled transactions take their positions; each short ID,
/// in order, takes the next free one. Two candidates for one short ID, or a
/// short ID repeated, is a collision, and nothing of the block is used.
[[nodiscard]] KN_API
compact_reconstruction reconstruct_compact_block(domain::message::compact_block const& message,
    domain::chain::transaction::list const& available);

/// The same, from the chain's mempool.
[[nodiscard]] KN_API
compact_reconstruction reconstruct_compact_block(domain::message::compact_block const& message,
    blockchain::block_chain const& chain);

/// Fill the positions a `missing` reconstruction lacks with `provided`, the
/// blocktxn answer, in order. Complete only when there is one for each
/// position, each has the short ID of its position, and the header commits to
/// the result.
[[nodiscard]] KN_API
compact_reconstruction accept_block_transactions(domain::message::compact_block const& message,
    compact_reconstruction const& partial, domain::chain::transaction::list const& provided);

/// Takes the cmpctblocks peers send and rebuilds them, each in a task of its
/// own: the read loop that offers one, and the blocktxn that answers it, never
/// waits. At most three are rebuilt at once, one per peer, as BIP152 expects
/// of a node asking three peers for high-bandwidth compact blocks; a slow peer
/// holds only its own.
class KN_API compact_block_receiver {
public:
    /// Reconstructions under way at once, each from a different peer.
    static constexpr size_t max_reconstructions = 3;

    /// How long a peer has to answer getblocktxn before the block is
    /// downloaded instead.
    static constexpr std::chrono::milliseconds default_blocktxn_timeout{10000};

    /// The chain and the network outlive run().
    compact_block_receiver(blockchain::block_chain& chain, p2p_node& network,
        std::chrono::milliseconds blocktxn_timeout = default_blocktxn_timeout);

    compact_block_receiver(compact_block_receiver const&) = delete;
    compact_block_receiver& operator=(compact_block_receiver const&) = delete;

    /// A cmpctblock from `peer`, from its read loop; never waits. True when it
    /// was taken to be rebuilt, or is the block another peer's cmpctblock is
    /// being rebuilt from: its header is then not for the header ingest. False
    /// when the node is not caught up, the block does not follow the connected
    /// tip, or the reconstructions are taken.
    [[nodiscard]]
    bool offer(network::peer_session::ptr const& peer, domain::message::compact_block message);

    /// A blocktxn from `peer`, from its read loop; never waits. True when the
    /// reconstruction that asked this peer for transactions took it.
    [[nodiscard]]
    bool deliver(network::peer_session::ptr const& peer, domain::message::block_transactions message);

    /// Until the network stops; then takes nothing more and waits for the
    /// reconstructions under way, which the stop ends.
    ::asio::awaitable<void> run();

    /// Reconstructions under way.
    [[nodiscard]]
    size_t pending() const;

private:
    using reply_channel = select_channel<domain::message::block_transactions>;

    struct reconstruction {
        hash_digest hash;
        /// Where the blocktxn goes once getblocktxn is out. One answer.
        std::shared_ptr<reply_channel> reply;
    };

    ::asio::awaitable<void> reconstruct(network::peer_session::ptr peer,
        domain::message::compact_block message);

    /// Whether the block was rebuilt and the chain took it (connected, or
    /// already held); false when it is to be downloaded instead.
    ::asio::awaitable<bool> rebuild(network::peer_session::ptr const& peer,
        domain::message::compact_block const& message);

    void fall_back(network::peer_session::ptr const& peer, domain::chain::header const& header);

    blockchain::block_chain& chain_;
    p2p_node& network_;
    std::chrono::milliseconds const blocktxn_timeout_;
    task_group tasks_;

    mutable std::mutex mutex_;
    bool stopping_{false};
    boost::unordered_flat_map<network::peer_session const*, reconstruction> pending_;
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_COMPACT_BLOCKS_HPP
