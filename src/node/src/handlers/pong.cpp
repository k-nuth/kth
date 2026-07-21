// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/handlers/pong.hpp>

#include <kth/domain.hpp>
#include <kth/node/p2p_node.hpp>

namespace kth::node::handlers::pong {
using namespace kth::network;

::asio::awaitable<message_result> handle(
    peer_session& peer,
    domain::message::pong const& msg)
{
    // Record pong to calculate latency
    if (peer.record_pong_received(msg.nonce())) {
        spdlog::trace("[pong] Received pong from [{}], nonce: {}, latency: {}ms",
            peer.authority(), msg.nonce(), peer.ping_latency_ms());
    } else {
        spdlog::trace("[pong] Received unexpected pong from [{}], nonce: {}",
            peer.authority(), msg.nonce());
    }
    co_return message_result::handled;
}

} // namespace kth::node::handlers::pong
