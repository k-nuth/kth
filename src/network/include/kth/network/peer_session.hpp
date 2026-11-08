// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_SESSION_HPP
#define KTH_NETWORK_PEER_SESSION_HPP

#include <kth/infrastructure/utility/platform_timer.hpp>
#if defined(KTH_BROWSER_NODE)
#include <kth/network/browser_stream.hpp>
#endif
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <variant>
#if defined(KTH_WITH_WEBSOCKET)
#include <kth/network/websocket_transport.hpp>
#endif

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/net_permissions.hpp>
#include <kth/network/settings.hpp>
#include <kth/network/tx_inventory.hpp>

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
//   // Receive the next message, or give up at a deadline
//   auto msg = co_await session->receive(std::chrono::steady_clock::now() + 5s);
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
#if defined(KTH_BROWSER_NODE)
    using socket_type = browser_stream;
#else
    using socket_type = ::asio::ip::tcp::socket;
#endif

    // Outbound messages, serialized and ready to write. Two queues with one
    // writer: control traffic (pings, pongs, requests, announcements) and
    // data served to the peer (blocks, headers). The writer always drains
    // control first, so a pong goes ahead of every queued block; it still
    // waits for a block already being written to the socket.
    // select_channels: executor-free, closed by stop().
    using outbound_channel = select_channel<data_chunk>;

    /// Messages the control queue holds before a send has to wait.
    static constexpr size_t outbound_capacity = 100;

    /// Messages the data queue holds before a send_data has to wait. Small:
    /// a served block can be large, and the queue is per peer.
    static constexpr size_t data_outbound_capacity = 2;

    /// Requests waiting for the node's per-peer service (see served_requests).
    static constexpr size_t served_requests_capacity = 16;

    // Inbound messages (heading + payload). A select_channel (see
    // async_channel.hpp): the read loop hands messages in with a suspended
    // send, consumers take them with receive(deadline), one selection with an
    // `after` case instead of `receive || timer`. A parallel group emits the
    // cancellation of its losing branch from the winning branch's completion
    // handler, unsynchronised with the other branch's completion on another
    // thread; that race freed a cancellation handler while it was being read
    // (SIGBUS in cancellation_slot::clear). A selection commits one case
    // under the channel's lock and has no such cancellation.
    using inbound_channel = select_channel<raw_message>;

    /// Construct a peer session from an established socket
    /// @param socket The connected socket
    /// @param settings Network settings
    /// @param inbound True if this is an inbound connection (we accepted it)
    peer_session(socket_type socket, settings const& settings, bool inbound = false);

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

    /// The strand all of this session's I/O completes on. The peer's whole
    /// lifecycle (handshake, protocols) runs on it, so what remains of
    /// `operator||` in that code (run()'s own loops) completes on one strand.
    [[nodiscard]]
    executor_type get_executor() const {
        return strand_;
    }

    // -------------------------------------------------------------------------
    // Messaging
    // -------------------------------------------------------------------------

    /// Send a control message to the peer. Waits while the control queue is
    /// full; error::channel_stopped once the session is stopped.
    template <typename Message>
    ::asio::awaitable<code> send(Message const& message) {
        if (stopped()) {
            co_return error::channel_stopped;
        }
        co_return co_await enqueue(outbound_,
            domain::message::serialize(version_.load(), message, protocol_magic_));
    }

    /// Send data served to the peer (blocks, headers answering its requests).
    /// Waits while the data queue is full, without holding control traffic.
    template <typename Message>
    ::asio::awaitable<code> send_data(Message const& message) {
        if (stopped()) {
            co_return error::channel_stopped;
        }
        co_return co_await enqueue(data_outbound_,
            domain::message::serialize(version_.load(), message, protocol_magic_));
    }

    /// Send raw bytes to the peer as control traffic.
    ::asio::awaitable<code> send_raw(data_chunk data);

    /// Bounded admission for task-owned responders. False requires explicit
    /// session closure or protocol debt handling; never silently drop a reply.
    bool try_send_raw(data_chunk data) {
        return !stopped() && outbound_.try_send(std::move(data));
    }

    bool is_websocket() const;


    // -------------------------------------------------------------------------
    // Served requests
    // -------------------------------------------------------------------------

    /// Requests the read loop hands to the node's per-peer service instead of
    /// answering them itself (getheaders, getdata): serving them waits on the
    /// data queue, and the read loop must keep reading. Bounded; the read loop
    /// only try_sends, and a peer that fills it has too many requests pending.
    /// Closed by stop(): a waiting service then sees eof.
    using served_channel = select_channel<raw_message>;

    [[nodiscard]]
    served_channel& served_requests();

    /// The data queue, for inspecting what is queued and not yet written.
    [[nodiscard]]
    outbound_channel& data_outbound();

    // -------------------------------------------------------------------------
    // Tip announcements
    // -------------------------------------------------------------------------

    /// The node's announcement debt to this peer: the latest connected tip
    /// not yet taken by the peer's service. Holds one hash; a newer tip
    /// replaces one not taken yet, so a peer that falls behind owes one
    /// announcement, never one per block. Closed by stop().
    using tip_channel = select_channel<hash_digest>;

    /// Record that `tip` is owed to this peer. One producer: the sync
    /// coordinator. Never waits.
    void announce_tip(hash_digest const& tip);

    [[nodiscard]]
    tip_channel& tip_announcements();

    /// The mempool's minimum fee per 1000 bytes, as it moved, not yet taken by
    /// the peer's service, which decides from it whether the fee filter it
    /// sends this peer is due sooner. Holds one value; a newer one replaces
    /// one not taken yet. Closed by stop().
    using fee_filter_channel = select_channel<uint64_t>;

    /// Record that the minimum moved to `fee_per_kb`. One producer: the
    /// node's fee filter watch. Never waits.
    void announce_fee_filter(uint64_t fee_per_kb);

    [[nodiscard]]
    fee_filter_channel& fee_filter_changes();

    // -------------------------------------------------------------------------
    // Transaction announcements
    // -------------------------------------------------------------------------

    /// The transactions admitted since the peer's service last looked: the
    /// last admission, which the service walks the mempool's order up to.
    /// Holds one; a newer one replaces one not taken yet. Closed by stop().
    using admission_channel = select_channel<tx_admissions>;

    /// Record that transactions were admitted up to `admissions.last`. One
    /// producer: the node's announcement watch. Never waits.
    void announce_admissions(tx_admissions const& admissions);

    [[nodiscard]]
    admission_channel& admissions();

    /// The peer asked for the mempool (BIP35) and may be answered. Holds
    /// one: requests coalesce until the service answers. Closed by stop().
    using mempool_request_channel = select_channel<std::monostate>;

    /// From the read loop, never waiting.
    void request_mempool();

    [[nodiscard]]
    mempool_request_channel& mempool_requests();

    /// What the peer knows and filters (tx_inventory). Only on this session's
    /// strand: its read loop and its service.
    [[nodiscard]]
    network::tx_inventory& tx_inventory() { return tx_inventory_; }

    /// The inbound message channel (for try_receive and selections of
    /// several cases; plain consumers use receive()).
    [[nodiscard]]
    inbound_channel& messages();

    /// The next inbound message, or by `deadline`: error::channel_timeout
    /// when the deadline passes first, error::channel_stopped once the
    /// session has closed the channel. A deadline that wins leaves the
    /// message in the channel.
    [[nodiscard]]
    awaitable_expected<raw_message> receive(
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

    // -------------------------------------------------------------------------
    // Direct I/O (for handshake before run() starts)
    // -------------------------------------------------------------------------
    // These methods read/write directly to the socket without using channels.
    // Use ONLY before calling run() - after run() starts, use send() and messages().
    // TCP only: WebSocket has an owned codec pump under run(); use the normal
    // channel-based handshake concurrently with run() for that transport.

    /// Read a message directly from the socket (bypasses inbound channel)
    /// Use this for handshake before run() is started. The reads are
    /// cancelled at `deadline` (error::channel_timeout).
    [[nodiscard]]
    awaitable_expected<raw_message> read_message_direct(
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

    /// Send a message directly to the socket (bypasses outbound channel)
    /// Use this for handshake before run() is started
    template <typename Message>
    ::asio::awaitable<code> send_direct(Message const& message) {
        if (is_websocket()) co_return error::not_implemented;
        if (stopped()) {
            co_return error::channel_stopped;
        }

        auto data = domain::message::serialize(version_.load(), message, protocol_magic_);
        auto [ec, bytes_written] = co_await write_exact(::asio::buffer(data));

        if (ec) {
            co_return error::boost_to_error_code(ec);
        }

        bytes_sent_.fetch_add(bytes_written, std::memory_order_relaxed);
        co_return error::success;
    }

    // -------------------------------------------------------------------------
    // Response channels (for request/response patterns like getdata/block)
    // -------------------------------------------------------------------------
    //
    // `headers` has no channel here: nothing on the wire says whether a
    // `headers` answers our `getheaders` or announces something, so every one
    // goes to the node's single header ingest instead of to a requester.
    //
    // These are select_channels (see async_channel.hpp): a requester waits on
    // one together with its deadline in ONE selection, so a deadline that fires
    // while a response is ready commits exactly one of the two — a losing
    // receive consumes nothing, and the response stays here for the next
    // request. The dispatcher hands responses in with a suspended send (no
    // executor, no async_send), and stop() closes them: a pending receive ends
    // with eof, a pending send with broken_pipe, and what is still buffered is
    // drained (see drain_response_channels).

    using response_channel = select_channel<raw_message>;

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

    /// Get formatted string with authority and user agent (for logging)
    /// Returns "[ip:port user_agent]" or "[ip:port]" if user agent not yet known
    [[nodiscard]]
    std::string authority_with_agent() const;

    /// Get/set the negotiated protocol version
    [[nodiscard]]
    uint32_t negotiated_version() const;
    void set_negotiated_version(uint32_t value);

    /// Get/set the peer's version message
    [[nodiscard]]
    domain::message::version::const_ptr peer_version() const;
    void set_peer_version(domain::message::version::const_ptr value);

    /// Get short user agent (e.g., "BCHN:28.0.1") - computed once from peer_version
    [[nodiscard]]
    std::string const& short_user_agent() const;

    /// Get/set the nonce for this connection
    [[nodiscard]]
    uint64_t nonce() const;
    void set_nonce(uint64_t value);

    /// Get/set whether to notify on new inventory
    [[nodiscard]]
    bool notify() const;
    void set_notify(bool value);

    /// Check if this is an inbound connection (peer connected to us)
    [[nodiscard]]
    bool is_inbound() const;

    /// Check if this is an outbound connection (we connected to peer)
    /// BCHN prefers outbound peers for sync (fPreferredDownload)
    [[nodiscard]]
    bool is_outbound() const;

    /// Check if peer is a full node (has NODE_NETWORK or NODE_NETWORK_LIMITED service)
    /// BCHN: fClient = !(services & NODE_NETWORK) && !(services & NODE_NETWORK_LIMITED)
    [[nodiscard]]
    bool is_full_node() const;

    /// Check if peer is a light client (no NODE_NETWORK service)
    [[nodiscard]]
    bool is_client() const;

    /// Check if this is a one-shot connection (connect, get addrs, disconnect)
    [[nodiscard]]
    bool is_one_shot() const;

    /// Mark this as a one-shot connection
    void set_one_shot(bool value);

    /// Check if peer has a specific permission
    [[nodiscard]]
    bool has_permission(permission_flags flag) const;

    /// Get all permission flags for this peer
    [[nodiscard]]
    permission_flags permissions() const;

    /// Set permission flags for this peer
    void set_permissions(permission_flags flags);

    /// Add a permission flag
    void add_permission(permission_flags flag);

    /// Remove a permission flag
    void clear_permission(permission_flags flag);

    /// Check if this peer is preferred for download (BCHN fPreferredDownload)
    /// fPreferredDownload = (!fInbound || HasPermission(PF_NOBAN)) && !fOneShot && !fClient
    [[nodiscard]]
    bool is_preferred_download() const;

    // -------------------------------------------------------------------------
    // Statistics (lock-free, benign data races acceptable)
    // -------------------------------------------------------------------------

    /// Get total bytes received from this peer
    [[nodiscard]]
    size_t bytes_received() const;

    /// Get total bytes sent to this peer
    [[nodiscard]]
    size_t bytes_sent() const;

    /// Get last ping latency in milliseconds
    [[nodiscard]]
    uint32_t ping_latency_ms() const;

    /// Record a ping sent (store nonce and time for latency calculation)
    void record_ping_sent(uint64_t nonce);

    /// Record a pong received, returns true if nonce matches pending ping
    bool record_pong_received(uint64_t nonce);

    /// Get connection time
    [[nodiscard]]
    std::chrono::steady_clock::time_point connection_time() const;

    // -------------------------------------------------------------------------
    // Misbehavior scoring (BCHN-style)
    // -------------------------------------------------------------------------

    /// Ban threshold - peer is banned when score reaches this value
    static constexpr int misbehavior_ban_threshold = 100;

    /// Score penalties for various misbehaviors
    static constexpr int misbehavior_timeout = 10;       // Timeout on request
    static constexpr int misbehavior_invalid_data = 100; // Invalid headers/blocks (instant ban)
    static constexpr int misbehavior_slow_peer = 100;    // Too slow for sync (instant ban after detection)

    /// Record misbehavior and return true if peer should be banned
    /// @param score Points to add to misbehavior score
    /// @return true if score now exceeds ban threshold
    bool misbehave(int score);

    /// Get current misbehavior score
    [[nodiscard]]
    int misbehavior_score() const;

    // -------------------------------------------------------------------------
    // BIP155 (addrv2) support
    // -------------------------------------------------------------------------

    /// Check if peer supports addrv2 (BIP155)
    /// True if peer sent sendaddrv2 during handshake
    [[nodiscard]]
    bool wants_addrv2() const;

    /// Mark that peer supports addrv2 (called when sendaddrv2 received)
    void set_wants_addrv2(bool value);

    // -------------------------------------------------------------------------
    // BIP130 (sendheaders)
    // -------------------------------------------------------------------------

    /// True once the peer sent sendheaders: new blocks are announced to it
    /// with headers instead of an inv.
    [[nodiscard]]
    bool prefers_headers() const;

    void set_prefers_headers(bool value);

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
    awaitable_expected<raw_message> read_message(
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

    /// Signal activity (resets inactivity timer)
    void signal_activity();

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------

    ::asio::awaitable<std::pair<std::error_code, size_t>> write_exact(::asio::const_buffer buffer);
#if defined(KTH_WITH_WEBSOCKET)
    friend awaitable_expected<peer_session::ptr> async_accept_websocket(socket_type, settings const&, websocket_server_options);
#endif

    // Socket and strand (all I/O serialized through strand)
    socket_type socket_;
    strand_type strand_;
#if defined(KTH_WITH_WEBSOCKET)
    std::unique_ptr<websocket_transport> websocket_;
#endif

    /// Queue `data` on `channel`, waiting while it is full.
    ::asio::awaitable<code> enqueue(outbound_channel& channel, data_chunk data);

    // Channels for message passing
    outbound_channel outbound_;
    outbound_channel data_outbound_;
    inbound_channel inbound_;
    served_channel served_requests_;
    tip_channel tip_announcements_;
    fee_filter_channel fee_filter_changes_;
    admission_channel admissions_;
    mempool_request_channel mempool_requests_;
    network::tx_inventory tx_inventory_;

    // Response channels for request/response patterns
    // These are filled by the dispatcher when specific responses arrive
    response_channel block_responses_;
    response_channel addr_responses_;

    // Timers
    ::kth::steady_timer inactivity_timer_;
    ::kth::steady_timer expiration_timer_;
    std::atomic<bool> activity_signaled_{false};

    // State
    std::atomic<bool> stopped_{false};

    /// Empty the per-request channels. Closing them is not enough: a closed
    /// channel still hands out what it already holds.
    void drain_response_channels();
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
    std::string short_user_agent_;  // Computed once from peer_version_ for logging

    // Connection direction (set at construction, never changes)
    bool const inbound_connection_;

    // Connection flags (can be set after construction)
    std::atomic<bool> one_shot_{false};
    std::atomic<uint32_t> permission_flags_{uint32_t(permission_flags::none)};

    // Statistics (lock-free, benign data races acceptable)
    std::atomic<size_t> bytes_received_{0};
    std::atomic<size_t> bytes_sent_{0};
    std::atomic<uint32_t> ping_latency_ms_{0};
    std::chrono::steady_clock::time_point connection_time_{std::chrono::steady_clock::now()};

    // Ping tracking (lock-free)
    // We store time as nanoseconds since steady_clock epoch for atomic access
    // Benign data races are acceptable for statistics
    // Zero means "no ping in flight", so a ping must never carry a zero nonce
    // (see p2p_node::generate_ping_nonce). A nonce we receive may be anything.
    std::atomic<uint64_t> pending_ping_nonce_{0};
    std::atomic<int64_t> pending_ping_time_ns_{0};

    // Misbehavior scoring (BCHN-style)
    std::atomic<int> misbehavior_score_{0};

    // BIP155 (addrv2) support
    // Set to true when peer sends sendaddrv2 during handshake
    std::atomic<bool> wants_addrv2_{false};

    // BIP130: set when the peer sends sendheaders
    std::atomic<bool> prefers_headers_{false};

    // Buffers (only accessed from read_loop, no synchronization needed)
    data_chunk heading_buffer_;
};

// =============================================================================
// Connection helpers - replace connector/acceptor classes
// =============================================================================

#if defined(KTH_WITH_WEBSOCKET)
[[nodiscard]] awaitable_expected<peer_session::ptr> async_accept_websocket(
    peer_session::socket_type socket, settings const& settings, websocket_server_options options);
#endif

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
