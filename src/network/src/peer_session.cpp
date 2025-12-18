// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/peer_session.hpp>

#include <chrono>
#include <utility>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using namespace std::chrono_literals;
using namespace ::asio::experimental::awaitable_operators;
using namespace kth::domain::message;

namespace {

// Helper to extract authority from socket
infrastructure::config::authority extract_authority(::asio::ip::tcp::socket const& socket) {
    boost_code ec;
    auto const endpoint = socket.remote_endpoint(ec);
    if (ec) {
        return {};
    }
    return infrastructure::config::authority(endpoint);
}

} // anonymous namespace

// =============================================================================
// Construction / Destruction
// =============================================================================

peer_session::peer_session(socket_type socket, settings const& settings, bool inbound)
    : socket_(std::move(socket))
    , strand_(socket_.get_executor())
    , outbound_(strand_, 100)  // Buffer up to 100 outbound messages
    , inbound_(strand_, 100)   // Buffer up to 100 inbound messages
    , headers_responses_(strand_, 10)  // Response channels for request/response patterns
    , block_responses_(strand_, 20)
    , addr_responses_(strand_, 10)
    , inactivity_timer_(strand_)
    , expiration_timer_(strand_)
    , authority_(extract_authority(socket_))
    , protocol_magic_(settings.identifier)
    , maximum_payload_(heading::maximum_payload_size(
          settings.protocol_maximum,
          settings.identifier,
          settings.inbound_port == 48333))
    , validate_checksum_(settings.validate_checksum)
    , inactivity_timeout_(std::chrono::minutes(settings.channel_inactivity_minutes))
    , expiration_timeout_(std::chrono::minutes(settings.channel_expiration_minutes))
    , version_(settings.protocol_maximum)
    , inbound_connection_(inbound)
    , heading_buffer_(heading::maximum_size())
{
    spdlog::debug("[peer_session] Constructor completed, authority: {}, inbound: {}",
        authority(), inbound_connection_);
}

peer_session::~peer_session() {
    stop(error::channel_stopped);
}

// =============================================================================
// Lifecycle
// =============================================================================

::asio::awaitable<code> peer_session::run() {
    spdlog::debug("[peer_session] run() starting for [{}]", authority());

    if (stopped()) {
        spdlog::debug("[peer_session] run() - already stopped [{}]", authority());
        co_return error::channel_stopped;
    }

    stopped_ = false;
    spdlog::debug("[peer_session] run() - about to start loops for [{}]", authority());

    try {
        // Run all loops concurrently - when any completes/fails, all stop
        co_await (
            read_loop() ||
            write_loop() ||
            inactivity_timer() ||
            expiration_timer()
        );
    } catch (std::exception const& e) {
        spdlog::debug("[peer_session] Exception in run loop [{}]: {}", authority(), e.what());
    }

    spdlog::debug("[peer_session] run() - loops completed for [{}]", authority());
    stop(error::channel_stopped);
    co_return error::channel_stopped;
}

void peer_session::stop(code const& ec) {
    if (stopped_.exchange(true)) {
        return;  // Already stopped
    }

    spdlog::debug("[peer_session] Stopping session [{}]: {}", authority(), ec.message());

    // Cancel timers (standalone Asio cancel() takes no args)
    inactivity_timer_.cancel();
    expiration_timer_.cancel();

    // Close channels
    outbound_.close();
    inbound_.close();

    // Shutdown socket
    boost_code ignore;
    socket_.shutdown(::asio::ip::tcp::socket::shutdown_both, ignore);
    socket_.cancel();
}

bool peer_session::stopped() const {
    return stopped_.load();
}

// =============================================================================
// Messaging
// =============================================================================

::asio::awaitable<code> peer_session::send_raw(data_chunk data) {
    if (stopped()) {
        co_return error::channel_stopped;
    }

    auto [ec] = co_await outbound_.async_send(
        std::error_code{},
        std::move(data),
        ::asio::as_tuple(::asio::use_awaitable));

    if (ec) {
        co_return error::channel_stopped;
    }
    co_return error::success;
}

peer_session::inbound_channel& peer_session::messages() {
    return inbound_;
}

peer_session::response_channel& peer_session::headers_responses() {
    return headers_responses_;
}

peer_session::response_channel& peer_session::block_responses() {
    return block_responses_;
}

peer_session::response_channel& peer_session::addr_responses() {
    return addr_responses_;
}

// =============================================================================
// Properties
// =============================================================================

infrastructure::config::authority const& peer_session::authority() const {
    return authority_;
}

uint32_t peer_session::negotiated_version() const {
    return version_.load();
}

void peer_session::set_negotiated_version(uint32_t value) {
    version_.store(value);
}

domain::message::version::const_ptr peer_session::peer_version() const {
    return peer_version_.load();
}

void peer_session::set_peer_version(domain::message::version::const_ptr value) {
    peer_version_.store(std::move(value));
}

uint64_t peer_session::nonce() const {
    return nonce_.load();
}

void peer_session::set_nonce(uint64_t value) {
    nonce_.store(value);
}

bool peer_session::notify() const {
    return notify_.load();
}

void peer_session::set_notify(bool value) {
    notify_.store(value);
}

bool peer_session::is_inbound() const {
    return inbound_connection_;
}

bool peer_session::is_outbound() const {
    return !inbound_connection_;
}

bool peer_session::is_full_node() const {
    auto version = peer_version();
    if (!version) {
        return false;  // Unknown, assume not full node
    }

    // BCHN: fClient = !(services & NODE_NETWORK) && !(services & NODE_NETWORK_LIMITED)
    // A full node has NODE_NETWORK or NODE_NETWORK_LIMITED
    auto const services = version->services();
    using svc = domain::message::version::service;
    return (services & svc::node_network) != 0
        || (services & svc::node_network_limited) != 0;
}

bool peer_session::is_client() const {
    return !is_full_node();
}

bool peer_session::is_one_shot() const {
    return one_shot_.load(std::memory_order_relaxed);
}

void peer_session::set_one_shot(bool value) {
    one_shot_.store(value, std::memory_order_relaxed);
}

bool peer_session::has_permission(permission_flags flag) const {
    auto const flags = permission_flags(permission_flags_.load(std::memory_order_relaxed));
    return ::kth::network::has_permission(flags, flag);
}

permission_flags peer_session::permissions() const {
    return permission_flags(permission_flags_.load(std::memory_order_relaxed));
}

void peer_session::set_permissions(permission_flags flags) {
    permission_flags_.store(uint32_t(flags), std::memory_order_relaxed);
}

void peer_session::add_permission(permission_flags flag) {
    auto current = permission_flags_.load(std::memory_order_relaxed);
    auto const new_flags = current | uint32_t(flag);
    permission_flags_.store(new_flags, std::memory_order_relaxed);
}

void peer_session::clear_permission(permission_flags flag) {
    auto current = permission_flags_.load(std::memory_order_relaxed);
    auto const new_flags = current & ~uint32_t(flag);
    permission_flags_.store(new_flags, std::memory_order_relaxed);
}

bool peer_session::is_preferred_download() const {
    // BCHN: fPreferredDownload = (!fInbound || HasPermission(PF_NOBAN))
    //                            && !fOneShot && !fClient
    return (is_outbound() || has_permission(permission_flags::noban))
        && !is_one_shot()
        && !is_client();
}

// =============================================================================
// Internal Coroutines
// =============================================================================

::asio::awaitable<void> peer_session::read_loop() {
    spdlog::debug("[peer_session] read_loop() starting for [{}]", authority());
    while (!stopped()) {
        auto result = co_await read_message();

        if (!result) {
            spdlog::debug("[peer_session] Read error [{}]: {}", authority(), result.error().message());
            stop(result.error());
            co_return;
        }

        // Signal activity to reset inactivity timer
        signal_activity();

        // Send message to inbound channel
        auto [ec] = co_await inbound_.async_send(
            std::error_code{},
            std::move(*result),
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[peer_session] Failed to enqueue message [{}]", authority());
            stop(error::channel_stopped);
            co_return;
        }
    }
}

::asio::awaitable<void> peer_session::write_loop() {
    spdlog::debug("[peer_session] write_loop() starting for [{}]", authority());
    while (!stopped()) {
        auto [ec, data] = co_await outbound_.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            // Channel closed or error
            co_return;
        }

        // Write to socket
        auto [write_ec, bytes_written] = co_await ::asio::async_write(
            socket_,
            ::asio::buffer(data),
            ::asio::as_tuple(::asio::use_awaitable));

        if (write_ec) {
            spdlog::debug("[peer_session] Write error [{}]: {}",
                authority(), write_ec.message());
            stop(error::boost_to_error_code(write_ec));
            co_return;
        }

        spdlog::trace("[peer_session] Sent {} bytes to [{}]", bytes_written, authority());
    }
}

::asio::awaitable<void> peer_session::inactivity_timer() {
    spdlog::debug("[peer_session] inactivity_timer() starting for [{}]", authority());
    while (!stopped()) {
        activity_signaled_ = false;
        inactivity_timer_.expires_after(inactivity_timeout_);

        auto [ec] = co_await inactivity_timer_.async_wait(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec == ::asio::error::operation_aborted) {
            // Timer was cancelled (reset or stop)
            continue;
        }

        if (ec) {
            co_return;
        }

        // Timer expired - check if there was activity
        if (!activity_signaled_.load()) {
            spdlog::debug("[peer_session] Inactivity timeout [{}]", authority());
            stop(error::channel_timeout);
            co_return;
        }
    }
}

::asio::awaitable<void> peer_session::expiration_timer() {
    spdlog::debug("[peer_session] expiration_timer() starting for [{}]", authority());
    expiration_timer_.expires_after(expiration_timeout_);

    auto [ec] = co_await expiration_timer_.async_wait(
        ::asio::as_tuple(::asio::use_awaitable));

    if (ec == ::asio::error::operation_aborted) {
        co_return;
    }

    spdlog::debug("[peer_session] Session expired [{}]", authority());
    stop(error::channel_timeout);
}

awaitable_expected<raw_message> peer_session::read_message() {
    // Read heading (24 bytes)
    auto [head_ec, head_bytes] = co_await ::asio::async_read(
        socket_,
        ::asio::buffer(heading_buffer_),
        ::asio::as_tuple(::asio::use_awaitable));

    if (head_ec) {
        co_return std::unexpected(error::boost_to_error_code(head_ec));
    }

    // Parse heading
    byte_reader reader(heading_buffer_);
    auto head_result = heading::from_data(reader, 0);

    if (!head_result) {
        spdlog::warn("[peer_session] Failed to parse heading from [{}]", authority());
        co_return std::unexpected(error::bad_stream);
    }

    auto const& head = *head_result;

    if (!head.is_valid()) {
        spdlog::warn("[peer_session] Invalid heading from [{}]", authority());
        co_return std::unexpected(error::bad_stream);
    }

    if (head.magic() != protocol_magic_) {
        spdlog::debug("[peer_session] Invalid magic ({}) from [{}]", head.magic(), authority());
        co_return std::unexpected(error::bad_stream);
    }

    if (head.payload_size() > maximum_payload_) {
        spdlog::debug("[peer_session] Oversized payload ({} bytes) from [{}]",
            head.payload_size(), authority());
        co_return std::unexpected(error::bad_stream);
    }

    // Read payload
    data_chunk payload(head.payload_size());

    if (head.payload_size() > 0) {
        auto [payload_ec, payload_bytes] = co_await ::asio::async_read(
            socket_,
            ::asio::buffer(payload),
            ::asio::as_tuple(::asio::use_awaitable));

        if (payload_ec) {
            co_return std::unexpected(error::boost_to_error_code(payload_ec));
        }
    }

    // Validate checksum if required
    if (validate_checksum_ && head.checksum() != bitcoin_checksum(payload)) {
        spdlog::warn("[peer_session] Invalid checksum for {} from [{}]",
            head.command(), authority());
        co_return std::unexpected(error::bad_stream);
    }

    spdlog::debug("[peer_session] Received {} from [{}] ({} bytes)",
        head.command(), authority(), head.payload_size());

    co_return raw_message{head, std::move(payload)};
}

void peer_session::signal_activity() {
    activity_signaled_ = true;
    // Cancel and restart the inactivity timer
    inactivity_timer_.cancel();
}

// =============================================================================
// Connection helpers - replace connector/acceptor classes
// =============================================================================

awaitable_expected<peer_session::ptr> async_connect(
    ::asio::any_io_executor executor,
    std::string const& hostname,
    uint16_t port,
    settings const& settings,
    std::chrono::seconds timeout)
{
    using namespace ::asio::experimental::awaitable_operators;

    // Create resolver
    ::asio::ip::tcp::resolver resolver(executor);

    // Resolve hostname
    auto [resolve_ec, endpoints] = co_await resolver.async_resolve(
        hostname,
        std::to_string(port),
        ::asio::as_tuple(::asio::use_awaitable));

    if (resolve_ec) {
        spdlog::debug("[async_connect] Failed to resolve {}: {}", hostname, resolve_ec.message());
        co_return std::unexpected(error::resolve_failed);
    }

    // Create socket and timer for timeout
    ::asio::ip::tcp::socket socket(executor);
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);

    // Race: connect vs timeout
    auto result = co_await (
        ::asio::async_connect(socket, endpoints, ::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    if (result.index() == 1) {
        // Timeout won
        spdlog::debug("[async_connect] Connection to {}:{} timed out", hostname, port);
        co_return std::unexpected(error::channel_timeout);
    }

    auto [connect_ec, endpoint] = std::get<0>(result);
    if (connect_ec) {
        spdlog::debug("[async_connect] Failed to connect to {}:{}: {}",
            hostname, port, connect_ec.message());
        co_return std::unexpected(error::boost_to_error_code(connect_ec));
    }

    spdlog::debug("[async_connect] Connected to {}:{}", hostname, port);
    spdlog::debug("[async_connect] Creating peer_session...");
    // Outbound connection (we connected to them)
    auto session = std::make_shared<peer_session>(std::move(socket), settings, /*inbound=*/false);
    spdlog::debug("[async_connect] peer_session created, authority: {}", session->authority());

    // Apply whitelist permissions (BCHN-style)
    auto flags = settings.get_whitelist_permissions(session->authority());
    if (flags != permission_flags::none) {
        flags = settings.apply_legacy_whitelist_permissions(flags);
        session->set_permissions(flags);
        spdlog::debug("[async_connect] Applied whitelist permissions to [{}]: {}",
            session->authority(), uint32_t(flags));
    }

    co_return session;
}

awaitable_expected<peer_session::ptr> async_connect(
    ::asio::any_io_executor executor,
    infrastructure::config::authority const& authority,
    settings const& settings,
    std::chrono::seconds timeout)
{
    co_return co_await async_connect(
        executor,
        authority.to_hostname(),
        authority.port(),
        settings,
        timeout);
}

awaitable_expected<peer_session::ptr> async_accept(
    ::asio::ip::tcp::acceptor& acceptor,
    settings const& settings)
{
    auto [ec, socket] = co_await acceptor.async_accept(
        ::asio::as_tuple(::asio::use_awaitable));

    if (ec) {
        if (ec == ::asio::error::operation_aborted) {
            co_return std::unexpected(error::service_stopped);
        }
        spdlog::debug("[async_accept] Accept failed: {}", ec.message());
        co_return std::unexpected(error::boost_to_error_code(ec));
    }

    boost_code peer_ec;
    auto const endpoint = socket.remote_endpoint(peer_ec);
    if (!peer_ec) {
        spdlog::debug("[async_accept] Accepted connection from {}:{}",
            endpoint.address().to_string(), endpoint.port());
    }

    // Inbound connection (they connected to us)
    auto session = std::make_shared<peer_session>(std::move(socket), settings, /*inbound=*/true);

    // Apply whitelist permissions (BCHN-style)
    auto flags = settings.get_whitelist_permissions(session->authority());
    if (flags != permission_flags::none) {
        flags = settings.apply_legacy_whitelist_permissions(flags);
        session->set_permissions(flags);
        spdlog::debug("[async_accept] Applied whitelist permissions to [{}]: {}",
            session->authority(), uint32_t(flags));
    }

    co_return session;
}

awaitable_expected<::asio::ip::tcp::acceptor> async_listen(
    ::asio::any_io_executor executor,
    uint16_t port)
{
    try {
        ::asio::ip::tcp::acceptor acceptor(executor);

        // Open, set options, bind, listen
        acceptor.open(::asio::ip::tcp::v6());
        acceptor.set_option(::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor.set_option(::asio::ip::v6_only(false));  // Accept both IPv4 and IPv6

        acceptor.bind(::asio::ip::tcp::endpoint(::asio::ip::tcp::v6(), port));
        acceptor.listen(::asio::socket_base::max_listen_connections);

        spdlog::info("[async_listen] Listening on port {}", port);
        co_return std::move(acceptor);

    } catch (std::system_error const& e) {
        spdlog::error("[async_listen] Failed to listen on port {}: {}", port, e.what());
        co_return std::unexpected(error::boost_to_error_code(e.code()));
    }
}

} // namespace kth::network
