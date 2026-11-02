// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_NODE_DETAIL_OUTBOUND_CONNECTIONS_HPP
#define KTH_NODE_DETAIL_OUTBOUND_CONNECTIONS_HPP
#include <algorithm>
#include <span>
#include <kth/network/peer_session.hpp>
namespace kth::node::detail {
// Incoming clients must never satisfy the outbound connectivity target.
inline size_t outbound_connection_count(std::span<network::peer_session::ptr const> peers) {
    return std::count_if(peers.begin(), peers.end(), [](auto const& peer) {
        return !peer->stopped() && peer->is_outbound();
    });
}
}
#endif
