// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/tx_announcements.hpp>

namespace kth::node::detail {

bool takes_transactions(network::peer_session const& peer) {
    auto const version = peer.peer_version();
    return version && version->relay();
}

void owe_transactions(std::vector<network::peer_session::ptr> const& peers, hash_list const& txids) {
    for (auto const& peer : peers) {
        if ( ! peer || peer->stopped() || ! takes_transactions(*peer)) {
            continue;
        }
        for (auto const& txid : txids) {
            peer->announce_transaction(txid);
        }
    }
}

} // namespace kth::node::detail
