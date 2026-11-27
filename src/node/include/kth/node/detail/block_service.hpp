// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_BLOCK_SERVICE_HPP
#define KTH_NODE_DETAIL_BLOCK_SERVICE_HPP

// Not installed. Which blocks a peer is asked for, from the services it
// advertises in its version message.
//
// - NODE_NETWORK: every block.
// - NODE_NETWORK_LIMITED alone (BIP159): the last 288 blocks below its tip,
//   the most such a peer is bound to serve. A deeper one is refused, and a
//   BCHN peer closes the connection that asks for it.
// - Neither, as from a light client or a pool's P2P listener: none, and no
//   headers either. Such a peer takes no part in the block download or in
//   the header walk (peer_session::is_full_node); it keeps every other role.

#include <algorithm>
#include <cstdint>

#include <kth/domain/message/version.hpp>

namespace kth::node::detail {

/// The fewest blocks below its tip a NODE_NETWORK_LIMITED peer serves (BIP159).
inline constexpr uint32_t limited_peer_blocks = 288;

/// The lowest height a peer that advertises `services` and reported
/// `peer_height` is asked for, in a range that ends at `range_end`.
///
/// For a NODE_NETWORK_LIMITED peer the tip is taken as the higher of the two
/// heights: the range ends at the header tip, and a peer ahead of it counts
/// its 288 from its own. Any other peer is asked from the start of the range;
/// one that serves no blocks is not given to the block download at all.
[[nodiscard]]
constexpr uint32_t lowest_height_served(uint64_t services, uint32_t peer_height, uint32_t range_end) {
    using service = domain::message::version::service;
    bool const limited_only = (services & service::node_network) == 0 &&
        (services & service::node_network_limited) != 0;
    if ( ! limited_only) {
        return 0;
    }
    auto const tip = std::max(range_end, peer_height);
    return tip < limited_peer_blocks ? 0 : tip - limited_peer_blocks + 1;
}

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_BLOCK_SERVICE_HPP
