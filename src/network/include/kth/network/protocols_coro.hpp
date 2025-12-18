// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOLS_CORO_HPP
#define KTH_NETWORK_PROTOCOLS_CORO_HPP

#include <chrono>
#include <expected>
#include <string>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

// =============================================================================
// Message Helpers
// =============================================================================

/// Wait for a specific message type from the peer
/// Returns the deserialized message or an error code
/// @param peer The peer session to receive from
/// @param timeout Maximum time to wait for the message
template <typename Message>
::asio::awaitable<std::expected<Message, code>> wait_for_message(
    peer_session& peer,
    std::chrono::seconds timeout)
{
    using namespace ::asio::experimental::awaitable_operators;

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, timeout);

    // Race between message receive and timeout
    auto result = co_await (
        peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    // Check which completed first
    if (result.index() == 1) {
        // Timeout won
        co_return std::unexpected(error::channel_timeout);
    }

    // Message received
    auto& [ec, raw] = std::get<0>(result);
    if (ec) {
        co_return std::unexpected(error::channel_stopped);
    }

    // Check command matches expected type
    if (raw.heading.command() != Message::command) {
        // Wrong message type - could handle differently
        co_return std::unexpected(error::bad_stream);
    }

    // Deserialize the message
    byte_reader reader(raw.payload);
    auto msg = Message::from_data(reader, peer.negotiated_version());
    if (!msg) {
        co_return std::unexpected(error::bad_stream);
    }

    co_return std::move(*msg);
}

/// Wait for any message from the peer (returns raw_message)
/// @param peer The peer session to receive from
/// @param timeout Maximum time to wait
::asio::awaitable<std::expected<raw_message, code>> wait_for_any_message(
    peer_session& peer,
    std::chrono::seconds timeout);

// =============================================================================
// Version Handshake Protocol
// =============================================================================

/// Configuration for version handshake
struct handshake_config {
    uint32_t protocol_version;      // Our protocol version
    uint64_t services;              // Our services
    uint64_t invalid_services;      // Services we reject
    uint32_t minimum_version;       // Minimum peer version we accept
    uint64_t minimum_services;      // Minimum peer services we accept
    std::string user_agent;         // Our user agent string
    uint32_t start_height;          // Our current block height
    uint64_t nonce;                 // Connection nonce
    std::chrono::seconds timeout;   // Handshake timeout
    std::vector<std::string> user_agent_blacklist;  // Blacklisted user agents
};

/// Result of a successful handshake
struct handshake_result {
    domain::message::version::const_ptr peer_version;
    uint32_t negotiated_version;
};

/// Perform version handshake with a peer
/// Exchanges version messages and negotiates protocol version
/// @param peer The peer session
/// @param config Handshake configuration
/// @return handshake_result on success, error code on failure
[[nodiscard]]
KN_API ::asio::awaitable<std::expected<handshake_result, code>> perform_handshake(
    peer_session& peer,
    handshake_config const& config);

/// Create handshake config from network settings
[[nodiscard]]
KN_API handshake_config make_handshake_config(
    settings const& network_settings,
    uint32_t current_height,
    uint64_t nonce);

// =============================================================================
// Ping/Pong Protocol
// =============================================================================

/// Run ping/pong loop with a peer
/// Sends periodic pings and responds to incoming pings
/// Runs until the peer is stopped or an error occurs
/// @param peer The peer session
/// @param ping_interval Time between pings
/// @return Error code when loop terminates
[[nodiscard]]
KN_API ::asio::awaitable<code> run_ping_pong(
    peer_session& peer,
    std::chrono::seconds ping_interval);

// =============================================================================
// Address Protocol
// =============================================================================

/// Request addresses from a peer
/// @param peer The peer session
/// @param timeout Maximum time to wait for response
/// @return Vector of addresses or error
[[nodiscard]]
KN_API ::asio::awaitable<std::expected<domain::message::address, code>> request_addresses(
    peer_session& peer,
    std::chrono::seconds timeout);

/// Send our addresses to a peer
/// @param peer The peer session
/// @param addresses Addresses to send
[[nodiscard]]
KN_API ::asio::awaitable<code> send_addresses(
    peer_session& peer,
    domain::message::address const& addresses);

} // namespace kth::network

#endif // KTH_NETWORK_PROTOCOLS_CORO_HPP
