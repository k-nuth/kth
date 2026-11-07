// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TRANSACTION_RELAY_HPP
#define KTH_NODE_TRANSACTION_RELAY_HPP

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

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

// What answers a peer's `getdata` for transactions: the requested mempool
// transactions, and the requested transactions the mempool does not have.
// Entries of other kinds are left to whatever serves them.
struct transaction_answer {
    std::vector<transaction_const_ptr> found;
    domain::message::inventory_vector::list missing;
};

KND_API transaction_answer answer_transaction_request(
    domain::message::get_data const& request, blockchain::mempool const& pool);

// Relays transactions between the peers and the mempool (#371). It asks a peer
// for the transactions it announces that the node has not seen, and offers each
// one to the mempool through organize(tx), the same entry sendrawtransaction
// uses, where the transaction organizer validates it. It announces every
// transaction the mempool admits, from a peer or from the RPC, to the peers
// that asked for transaction relay in their version message, and answers their
// requests for the mempool's transactions.
class KND_API transaction_relay {
public:
    // How many txids are remembered as asked for or known.
    static constexpr std::size_t remembered_txids = 50000;

    transaction_relay(blockchain::block_chain& chain, p2p_node& network);

    // inv: ask the peer for the transactions it announces that the node lacks.
    // Ignored while the node is not synchronized: it has nothing current to
    // check them against.
    ::asio::awaitable<message_result> on_inventory(
        network::peer_session& peer, domain::message::inventory const& inv);

    // tx: offer it to the mempool.
    ::asio::awaitable<message_result> on_transaction(
        network::peer_session& peer, domain::message::transaction const& tx);

    // getdata: send the requested mempool transactions, and notfound for the
    // requested transactions the mempool does not have.
    ::asio::awaitable<message_result> on_get_data(
        network::peer_session& peer, domain::message::get_data const& request);

    // Announce a transaction the mempool admitted to every peer that takes
    // transaction relay: one that did not set `relay` false in its version.
    ::asio::awaitable<void> announce(transaction_const_ptr tx);

private:
    blockchain::block_chain& chain_;
    p2p_node& network_;
    recent_txids recent_;
};

} // namespace kth::node

#endif // KTH_NODE_TRANSACTION_RELAY_HPP
