// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_HANDLERS_PONG_HPP
#define KTH_NETWORK_HANDLERS_PONG_HPP

#include <kth/domain/message/pong.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

#include <asio/awaitable.hpp>

namespace kth::network {

enum class message_result;

namespace handlers::pong {

/// Handle incoming pong message - just acknowledge
/// Receives the already-parsed pong message
[[nodiscard]]
KN_API ::asio::awaitable<message_result> handle(
    peer_session& peer,
    domain::message::pong const& msg);

} // namespace handlers::pong

} // namespace kth::network

#endif // KTH_NETWORK_HANDLERS_PONG_HPP
