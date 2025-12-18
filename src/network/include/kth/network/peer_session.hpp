// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_SESSION_HPP
#define KTH_NETWORK_PEER_SESSION_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

// Asio includes for coroutines
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/as_tuple.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::network {

using kth::awaitable_expected;

// =============================================================================
// peer_session: Modern coroutine-based peer connection handler
// =============================================================================
//
// Replaces the legacy proxy/channel classes with a simpler coroutine-based
// implementation. All I/O operations are serialized through a strand,
// eliminating the need for explicit mutexes.
//
// Usage:
//   auto session = std::make_shared<peer_session>(std::move(socket), settings);
//   co_spawn(executor, session->run(), detached);
//
//   // Send a message
//   co_await session->send(version_message);
//
//   // Receive messages via channel
//   auto [ec, msg] = co_await session->messages().async_receive(...);
//
// =============================================================================

/// Represents a raw message received from a peer
struct raw_message {
    domain::message::heading heading;
    data_chunk payload;
};

/// Modern coroutine-based peer session, replaces proxy/channel
class KN_API peer_session : public std::enable_shared_from_this<peer_session> {
public:
    using ptr = std::shared_ptr<peer_session>;
    using executor_type = ::asio::any_io_executor;
    using strand_type = ::asio::strand<executor_type>;
    using socket_type = ::asio::ip::tcp::socket;

    // Channel for outbound messages (serialized bytes ready to send)
    using outbound_channel = concurrent_channel<data_chunk>;

    // Channel for inbound messages (raw message with heading + payload)
    using inbound_channel = concurrent_channel<raw_message>;

    /// Construct a peer session from an established socket
    peer_session(socket_type socket, settings const& settings);

    /// Destructor - ensures clean shutdown
    ~peer_session();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Main coroutine - runs read/write/timer loops concurrently
    /// Returns when the session is stopped or an error occurs
    [[nodiscard]]
    ::asio::awaitable<code> run();

    /// Stop the session gracefully
    void stop(code const& ec = error::channel_stopped);

    /// Check if the session is stopped
    [[nodiscard]]
    bool stopped() const;

    // -------------------------------------------------------------------------
    // Messaging
    // -------------------------------------------------------------------------

    /// Send a message to the peer (coroutine version)
    template <typename Message>
    ::asio::awaitable<code> send(Message const& message) {
        if (stopped()) {
            co_return error::channel_stopped;
        }

        auto data = domain::message::serialize(version_.load(), message, protocol_magic_);
        auto [ec] = co_await outbound_.async_send(
            std::error_code{},
            std::move(data),
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            co_return error::channel_stopped;
        }
        co_return error::success;
    }

    /// Send raw bytes to the peer (coroutine version)
    ::asio::awaitable<code> send_raw(data_chunk data);

    /// Get the inbound message channel for receiving messages
    [[nodiscard]]
    inbound_channel& messages();

    // -------------------------------------------------------------------------
    // Response channels (for request/response patterns like getheaders/headers)
    // -------------------------------------------------------------------------

    /// Channel for headers responses (filled by message handler when headers arrives)
    using response_channel = concurrent_channel<raw_message>;

    /// Get the channel for headers responses
    /// The dispatcher should route 'headers' messages here when someone is waiting
    [[nodiscard]]
    response_channel& headers_responses();

    /// Get the channel for block responses
    /// The dispatcher should route 'block' messages here when someone is waiting
    [[nodiscard]]
    response_channel& block_responses();

    /// Get the channel for address responses
    /// The dispatcher should route 'addr' messages here when someone is waiting
    [[nodiscard]]
    response_channel& addr_responses();

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------

    /// Get the authority (address:port) of the remote peer
    [[nodiscard]]
    infrastructure::config::authority const& authority() const;

    /// Get/set the negotiated protocol version
    [[nodiscard]]
    uint32_t negotiated_version() const;
    void set_negotiated_version(uint32_t value);

    /// Get/set the peer's version message
    [[nodiscard]]
    domain::message::version::const_ptr peer_version() const;
    void set_peer_version(domain::message::version::const_ptr value);

    /// Get/set the nonce for this connection
    [[nodiscard]]
    uint64_t nonce() const;
    void set_nonce(uint64_t value);

    /// Get/set whether to notify on new inventory
    [[nodiscard]]
    bool notify() const;
    void set_notify(bool value);

private:
    // -------------------------------------------------------------------------
    // Internal coroutines
    // -------------------------------------------------------------------------

    /// Read loop - reads messages from socket and dispatches to inbound channel
    ::asio::awaitable<void> read_loop();

    /// Write loop - processes outbound channel and writes to socket
    ::asio::awaitable<void> write_loop();

    /// Inactivity timer - stops session if no messages received
    ::asio::awaitable<void> inactivity_timer();

    /// Expiration timer - stops session after lifetime expires
    ::asio::awaitable<void> expiration_timer();

    /// Read a complete message (heading + payload)
    awaitable_expected<raw_message> read_message();

    /// Signal activity (resets inactivity timer)
    void signal_activity();

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------

    // Socket and strand (all I/O serialized through strand)
    socket_type socket_;
    strand_type strand_;

    // Channels for message passing
    outbound_channel outbound_;
    inbound_channel inbound_;

    // Response channels for request/response patterns
    // These are filled by the dispatcher when specific responses arrive
    response_channel headers_responses_;
    response_channel block_responses_;
    response_channel addr_responses_;

    // Timers
    ::asio::steady_timer inactivity_timer_;
    ::asio::steady_timer expiration_timer_;
    std::atomic<bool> activity_signaled_{false};

    // State
    std::atomic<bool> stopped_{false};
    infrastructure::config::authority authority_;

    // Protocol settings
    uint32_t const protocol_magic_;
    size_t const maximum_payload_;
    bool const validate_checksum_;
    std::chrono::seconds const inactivity_timeout_;
    std::chrono::seconds const expiration_timeout_;

    // Negotiated values (atomic for thread-safe access)
    std::atomic<uint32_t> version_;
    std::atomic<uint64_t> nonce_{0};
    std::atomic<bool> notify_{true};
    kth::atomic<domain::message::version::const_ptr> peer_version_;

    // Buffers (only accessed from read_loop, no synchronization needed)
    data_chunk heading_buffer_;
};

// =============================================================================
// Connection helpers - replace connector/acceptor classes
// =============================================================================

/// Connect to a peer by hostname and port
/// Resolves DNS and connects with timeout
/// @param executor The executor to use for async operations
/// @param hostname The hostname or IP address to connect to
/// @param port The port number
/// @param settings Network settings for the connection
/// @param timeout Connection timeout
/// @return awaitable that yields a peer_session::ptr or error code
[[nodiscard]]
awaitable_expected<peer_session::ptr> async_connect(
    ::asio::any_io_executor executor,
    std::string const& hostname,
    uint16_t port,
    settings const& settings,
    std::chrono::seconds timeout);

/// Connect to a peer by authority (address:port)
[[nodiscard]]
awaitable_expected<peer_session::ptr> async_connect(
    ::asio::any_io_executor executor,
    infrastructure::config::authority const& authority,
    settings const& settings,
    std::chrono::seconds timeout);

/// Accept a single incoming connection
/// @param acceptor An already-listening TCP acceptor
/// @param settings Network settings for the connection
/// @return awaitable that yields a peer_session::ptr or error code
[[nodiscard]]
awaitable_expected<peer_session::ptr> async_accept(
    ::asio::ip::tcp::acceptor& acceptor,
    settings const& settings);

/// Start listening on a port and return an acceptor
/// @param executor The executor to use
/// @param port The port to listen on
/// @return awaitable that yields an acceptor or error code
[[nodiscard]]
awaitable_expected<::asio::ip::tcp::acceptor> async_listen(
    ::asio::any_io_executor executor,
    uint16_t port);

} // namespace kth::network

#endif // KTH_NETWORK_PEER_SESSION_HPP
