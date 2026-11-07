// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TRANSACTION_RELAY_HPP
#define KTH_NODE_TRANSACTION_RELAY_HPP

#include <cstddef>
#include <deque>
#include <mutex>

#include <asio/awaitable.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>
#include <kth/node/p2p_node.hpp>

namespace kth::node {

// Txids already asked for or already known, so that a transaction several
// peers announce is requested once. Bounded: the oldest are forgotten first.
// Thread-safe.
class KND_API recent_txids {
public:
    explicit recent_txids(std::size_t capacity);

    // Whether `txid` was remembered before; it is remembered either way.
    bool remember(hash_digest const& txid);

private:
    std::size_t const capacity_;
    std::mutex mutex_;
    boost::unordered_flat_set<hash_digest> set_;
    std::deque<hash_digest> order_;
};

// The transaction entries of `inv` worth asking for: not in the mempool and not
// remembered, each remembered as it is chosen.
KND_API domain::message::inventory_vector::list transactions_to_request(
    domain::message::inventory const& inv,
    blockchain::mempool const& pool, recent_txids& recent);

// Relays transactions between the peers and the mempool (#371). It asks a peer
// for the transactions it announces that the node has not seen, and offers each
// one to the mempool through organize(tx), the same entry sendrawtransaction
// uses, where the transaction organizer validates it.
class KND_API transaction_relay {
public:
    // How many txids are remembered as asked for or known.
    static constexpr std::size_t remembered_txids = 50000;

    explicit transaction_relay(blockchain::block_chain& chain);

    // inv: ask the peer for the transactions it announces that the node lacks.
    // Ignored while the node is not synchronized: it has nothing current to
    // check them against.
    ::asio::awaitable<message_result> on_inventory(
        network::peer_session& peer, domain::message::inventory const& inv);

    // tx: offer it to the mempool.
    ::asio::awaitable<message_result> on_transaction(
        network::peer_session& peer, domain::message::transaction const& tx);

private:
    blockchain::block_chain& chain_;
    recent_txids recent_;
};

} // namespace kth::node

#endif // KTH_NODE_TRANSACTION_RELAY_HPP
