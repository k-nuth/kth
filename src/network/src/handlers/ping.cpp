// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/handlers/ping.hpp>

#include <kth/domain.hpp>
#include <kth/network/p2p_node.hpp>

namespace kth::network::handlers::ping {

::asio::awaitable<message_result> handle(
    peer_session& peer,
    domain::message::ping const& msg)
{
    // Respond with pong using the same nonce
    domain::message::pong pong_msg(msg.nonce());
    auto ec = co_await peer.send(pong_msg);

    if (ec != error::success) {
        spdlog::debug("[ping] Failed to send pong to [{}]", peer.authority());
    } else {
        spdlog::debug("[ping] Responded pong to [{}]", peer.authority());
    }

    co_return message_result::handled;
}

} // namespace kth::network::handlers::ping
