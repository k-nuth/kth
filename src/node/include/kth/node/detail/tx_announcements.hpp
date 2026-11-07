// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP
#define KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP

// Not installed. The transactions the node owes its peers an announcement of.
//
// Each transaction the mempool admits, from a peer or from the RPC, is owed to
// every peer that asked for transactions in its version message (BIP37
// `relay`). Recording that never waits: the peer's service takes what it is
// owed and sends it as one inv (message_dispatcher::serve), so a peer that does
// not keep up delays only itself and, past an inv's worth, is not told of newer
// transactions.

#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node::detail {

/// Whether `peer` asked for transactions: its version message did not set
/// `relay` false. A peer whose version is not known yet did not ask.
[[nodiscard]]
bool takes_transactions(network::peer_session const& peer);

/// Record each of `txids` as owed to each of `peers` that takes transactions
/// and has not stopped.
void owe_transactions(std::vector<network::peer_session::ptr> const& peers, hash_list const& txids);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP
