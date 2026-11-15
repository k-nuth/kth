// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_OUTBOUND_CONNECTIONS_HPP
#define KTH_NODE_DETAIL_OUTBOUND_CONNECTIONS_HPP

#include <cstddef>
#include <span>

#include <kth/network/peer_session.hpp>

namespace kth::node::detail {

struct peer_counts {
    size_t outbound = 0;
    size_t inbound = 0;
};

/// The live peers by direction. Only outbound ones count toward the outbound
/// target: peers that connected to this node (browser clients over WebSocket
/// among them) never stand in for the connections it opens itself.
inline peer_counts count_peers(std::span<network::peer_session::ptr const> peers) {
    peer_counts counts;
    for (auto const& peer : peers) {
        if (peer->stopped()) continue;
        ++(peer->is_inbound() ? counts.inbound : counts.outbound);
    }
    return counts;
}

} // namespace kth::node::detail

#endif
