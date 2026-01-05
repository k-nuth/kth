// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/handlers/pong.hpp>

#include <kth/domain.hpp>
#include <kth/network/p2p_node.hpp>

namespace kth::network::handlers::pong {

::asio::awaitable<message_result> handle(
    peer_session& peer,
    domain::message::pong const& msg)
{
    // Just acknowledge receipt - could verify nonce if we tracked sent pings
    spdlog::trace("[pong] Received pong from [{}], nonce: {}", peer.authority(), msg.nonce());
    co_return message_result::handled;
}

} // namespace kth::network::handlers::pong
