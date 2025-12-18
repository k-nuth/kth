// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_HANDLERS_PING_HPP
#define KTH_NETWORK_HANDLERS_PING_HPP

#include <kth/domain/message/ping.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

#include <asio/awaitable.hpp>

namespace kth::network {

enum class message_result;

namespace handlers::ping {

/// Handle incoming ping message - respond with pong
/// Receives the already-parsed ping message
[[nodiscard]]
KN_API ::asio::awaitable<message_result> handle(
    peer_session& peer,
    domain::message::ping const& msg);

} // namespace handlers::ping

} // namespace kth::network

#endif // KTH_NETWORK_HANDLERS_PING_HPP
