// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/peer_session.hpp>

#include <chrono>
#include <format>
#include <string_view>
#include <utility>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using namespace std::chrono_literals;
using namespace ::asio::experimental::awaitable_operators;
using namespace kth::domain::message;

namespace {

// Map from long client name to short name for logging
// "/Bitcoin Cash Node:28.0.1(EB32.0)/" -> "BCHN:28.0.1"
boost::unordered_flat_map<std::string_view, std::string_view> const known_clients = {
    {"Bitcoin Cash Node:", "BCHN"},
    {"Knuth:", "KTH"},
    {"Bitcoin ABC:", "ABC"},
    {"Bitcoin XT:", "XT"},
    {"BitcoinUnlimited:", "BU"},
    {"BCH Unlimited:", "BU"},
    {"BUCash:", "BU"},
};

// Summarize user agent for logging
std::string summarize_user_agent(std::string const& user_agent) {
    if (user_agent.empty()) {
        return "Unknown";
    }

    for (auto const& [search, short_name] : known_clients) {
        auto pos = user_agent.find(search);
        if (pos != std::string::npos) {
            // Extract version: starts after the search string, ends at '(' or '/' or end
            auto version_start = pos + search.size();
            auto version_end = user_agent.find_first_of("(/", version_start);
            if (version_end == std::string::npos) {
                version_end = user_agent.size();
            }
            auto version = user_agent.substr(version_start, version_end - version_start);
            return std::format("{}:{}", short_name, version);
        }
    }

    return "Other";
}

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
    // First, ensure stopped flag is set and operations are canceled
    stop(error::channel_stopped);

    // Now it's safe to close everything - run() has completed so no || operator
    // is active, and no operations are pending.
    boost_code ignore;
    socket_.close(ignore);

    // Cancel before close to ensure any pending async ops are woken up
    outbound_.cancel();
    inbound_.cancel();
    headers_responses_.cancel();
    block_responses_.cancel();
    addr_responses_.cancel();

    outbound_.close();
    inbound_.close();
    headers_responses_.close();
    block_responses_.close();
    addr_responses_.close();
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

    // 2026-01-28: Fix for ASAN heap-use-after-free in cancellation_signal::emit()
    //
    // ROOT CAUSE: When using the || operator with a multi-threaded io_context,
    // the parallel_group's internal cancellation mechanism has a race condition:
    // 1. Awaitable A completes on thread 1
    // 2. parallel_group_cancellation_handler::operator() tries to cancel awaitable B
    // 3. Awaitable B might be completing simultaneously on thread 2
    // 4. The cancellation_signal::emit() accesses memory being freed
    //
    // FIX: Bind all operations to the strand, so they are serialized. This ensures
    // the || operator's internal bookkeeping runs on a single strand, preventing
    // concurrent access to cancellation state.
    //
    // We use bind_executor to wrap each loop coroutine with the strand, ensuring
    // all operations (including cancellation) run on the strand.

    try {
        // Run all loops concurrently on the strand - when any completes/fails, all stop
        // Using co_spawn with use_awaitable ensures all operations run on the strand,
        // which serializes the || operator's cancellation mechanism.
        co_await (
            ::asio::co_spawn(strand_, read_loop(), ::asio::use_awaitable) ||
            ::asio::co_spawn(strand_, write_loop(), ::asio::use_awaitable) ||
            ::asio::co_spawn(strand_, inactivity_timer(), ::asio::use_awaitable) ||
            ::asio::co_spawn(strand_, expiration_timer(), ::asio::use_awaitable)
        );
    } catch (std::exception const& e) {
        spdlog::debug("[peer_session] Exception in run loop [{}]: {}", authority(), e.what());
    }

    spdlog::debug("[peer_session] run() - loops completed for [{}]", authority());
    stop(error::channel_stopped);
    co_return error::channel_stopped;
}

void peer_session::stop(code const& ec) {
    // 2026-01-28: Fix for ASAN heap-use-after-free in cancellation_signal::emit()
    //
    // ROOT CAUSE: When stop() is called from a different thread while run() is
    // executing the || operator (read_loop || write_loop || ...), closing channels
    // directly causes a race condition:
    // 1. Channel close triggers completion of pending async operations
    // 2. The || operator tries to cancel other awaitables via cancellation_signal
    // 3. But the cancellation handlers may reference already-freed memory
    //
    // FIX: Don't close channels in stop(). Only close the socket, which safely
    // causes read_loop to exit with an error. The || operator will then cancel
    // the other loops through its normal cancellation mechanism. Channels are
    // closed in the destructor when refcount reaches 0 (no pending operations).

    spdlog::debug("[peer_session] stop() checkpoint 1: entering, ec={}", ec.value());

    if (stopped_.exchange(true)) {
        spdlog::debug("[peer_session] stop() checkpoint 1b: already stopped, returning");
        return;  // Already stopped
    }

    spdlog::debug("[peer_session] stop() checkpoint 2: got lock");
    auto const auth_str = authority().to_string();
    spdlog::debug("[peer_session] stop() checkpoint 3: got authority {}", auth_str);
    auto const ec_msg = ec.message();
    spdlog::debug("[peer_session] stop() checkpoint 4: got ec message {}", ec_msg);
    spdlog::debug("[peer_session] Stopping session [{}]: {}", auth_str, ec_msg);
    spdlog::debug("[peer_session] stop() checkpoint 5: canceling timers");

    // Cancel timers first - this is safe from any thread
    inactivity_timer_.cancel();
    spdlog::debug("[peer_session] stop() checkpoint 6: inactivity_timer canceled");
    expiration_timer_.cancel();
    spdlog::debug("[peer_session] stop() checkpoint 7: expiration_timer canceled");

    // Cancel pending socket operations - this causes read_loop to exit with an error.
    // The || operator will then cancel write_loop and other awaitables.
    // IMPORTANT: Use cancel() instead of close()! close() is not thread-safe when
    // there are pending async operations - it can corrupt internal epoll_reactor state.
    // The socket will be properly closed when peer_session is destroyed.
    spdlog::debug("[peer_session] stop() checkpoint 8: canceling socket");
    boost_code ignore;
    socket_.shutdown(::asio::ip::tcp::socket::shutdown_both, ignore);
    spdlog::debug("[peer_session] stop() checkpoint 9: socket shutdown");
    socket_.cancel(ignore);
    spdlog::debug("[peer_session] stop() checkpoint 10: socket canceled");

    // 2026-01-29: Close ALL channels to unblock any coroutines waiting on them.
    //
    // This fixes slow shutdown: when protocols (request_headers, request_blocks, etc.)
    // are waiting on channels with long timeouts (30s for responses, 2min for ping timer),
    // closing the channels makes async_receive()/async_send() return immediately with error.
    //
    // Previously this caused ASAN heap-use-after-free, but that was fixed by making
    // run()'s || operator execute on the strand (co_spawn with strand_).
    //
    // 2026-02-02: CRITICAL FIX - call cancel() BEFORE close()!
    // close() marks the channel as closed but may not immediately complete pending operations.
    // cancel() explicitly "cancels all asynchronous operations waiting on the channel".
    // Without this, run_peer_protocols() using || operator can hang indefinitely because
    // the async_receive() doesn't complete when only close() is called.
    //
    // Channels:
    // - inbound_: Used by run_peer_protocols() waiting for messages
    // - outbound_: Used by write_loop() and send operations
    // - headers_responses_, block_responses_, addr_responses_: Used by request protocols
    spdlog::debug("[peer_session] stop() checkpoint 11: canceling all channel operations");
    inbound_.cancel();
    outbound_.cancel();
    headers_responses_.cancel();
    block_responses_.cancel();
    addr_responses_.cancel();
    spdlog::debug("[peer_session] stop() checkpoint 11b: closing all channels");
    inbound_.close();
    outbound_.close();
    headers_responses_.close();
    block_responses_.close();
    addr_responses_.close();
    spdlog::debug("[peer_session] stop() checkpoint 12: all channels closed, done");
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

std::string peer_session::authority_with_agent() const {
    if ( ! short_user_agent_.empty()) {
        return std::format("{} {}", authority_.to_string(), short_user_agent_);
    }
    return authority_.to_string();
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
    if (value) {
        short_user_agent_ = summarize_user_agent(value->user_agent());
    }
    peer_version_.store(std::move(value));
}

std::string const& peer_session::short_user_agent() const {
    return short_user_agent_;
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
// Statistics (lock-free, benign data races acceptable)
// =============================================================================

size_t peer_session::bytes_received() const {
    return bytes_received_.load(std::memory_order_relaxed);
}

size_t peer_session::bytes_sent() const {
    return bytes_sent_.load(std::memory_order_relaxed);
}

uint32_t peer_session::ping_latency_ms() const {
    return ping_latency_ms_.load(std::memory_order_relaxed);
}

void peer_session::record_ping_sent(uint64_t nonce) {
    // Store time as nanoseconds since steady_clock epoch for lock-free access
    auto const now = std::chrono::steady_clock::now();
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    pending_ping_time_ns_.store(ns, std::memory_order_relaxed);
    pending_ping_nonce_.store(nonce, std::memory_order_relaxed);
}

bool peer_session::record_pong_received(uint64_t nonce) {
    auto const expected_nonce = pending_ping_nonce_.load(std::memory_order_relaxed);
    if (expected_nonce == 0 || nonce != expected_nonce) {
        return false;
    }

    auto const sent_ns = pending_ping_time_ns_.load(std::memory_order_relaxed);
    auto const now = std::chrono::steady_clock::now();
    auto const now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    auto const latency_ms = uint32_t((now_ns - sent_ns) / 1'000'000);
    ping_latency_ms_.store(latency_ms, std::memory_order_relaxed);
    pending_ping_nonce_.store(0, std::memory_order_relaxed);
    return true;
}

std::chrono::steady_clock::time_point peer_session::connection_time() const {
    return connection_time_;
}

// =============================================================================
// Misbehavior Scoring
// =============================================================================

bool peer_session::misbehave(int score) {
    auto const new_score = misbehavior_score_.fetch_add(score, std::memory_order_relaxed) + score;
    return new_score >= misbehavior_ban_threshold;
}

int peer_session::misbehavior_score() const {
    return misbehavior_score_.load(std::memory_order_relaxed);
}

// =============================================================================
// BIP155 (addrv2) support
// =============================================================================

bool peer_session::wants_addrv2() const {
    return wants_addrv2_.load(std::memory_order_relaxed);
}

void peer_session::set_wants_addrv2(bool value) {
    wants_addrv2_.store(value, std::memory_order_relaxed);
}

// =============================================================================
// Internal Coroutines
// =============================================================================

::asio::awaitable<void> peer_session::read_loop() {
    spdlog::debug("[peer_session] read_loop() starting for [{}]", authority());
    while (!stopped()) {
        auto result = co_await read_message();

        if (!result) {
            // 2026-01-28: Debug checkpoints for segfault during header sync.
            // Symptom: crash during "Read error" logging, log truncated mid-IP address
            // e.g., "[peer_session] Read error [3.228.193." then crash
            spdlog::debug("[peer_session] read_loop checkpoint 1: read_message returned error");
            auto const err = result.error();
            spdlog::debug("[peer_session] read_loop checkpoint 2: got error code {}", err.value());
            auto const auth_str = authority().to_string();
            spdlog::debug("[peer_session] read_loop checkpoint 3: got authority {}", auth_str);
            auto const err_msg = err.message();
            spdlog::debug("[peer_session] read_loop checkpoint 4: got error message {}", err_msg);
            spdlog::debug("[peer_session] Read error [{}]: {}", auth_str, err_msg);
            spdlog::debug("[peer_session] read_loop checkpoint 5: logged error, calling stop()");
            stop(err);
            spdlog::debug("[peer_session] read_loop checkpoint 6: stop() returned, co_return");
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

        // Track bytes sent (before clearing data)
        bytes_sent_.fetch_add(bytes_written, std::memory_order_relaxed);

        // Explicitly release buffer memory before next iteration
        // This prevents leaks when the coroutine is cancelled while waiting on async_receive
        data.clear();
        data.shrink_to_fit();

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
            head.command(), authority_with_agent());
        co_return std::unexpected(error::bad_stream);
    }

    // Track bytes received (header + payload)
    bytes_received_.fetch_add(heading::maximum_size() + head.payload_size(), std::memory_order_relaxed);

    spdlog::debug("[peer_session] Received {} from [{}] ({} bytes)",
        head.command(), authority_with_agent(), head.payload_size());

    co_return raw_message{head, std::move(payload)};
}

// =============================================================================
// Direct I/O (for handshake before run() starts)
// =============================================================================

awaitable_expected<raw_message> peer_session::read_message_direct() {
    // Just delegate to the private read_message() implementation
    return read_message();
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

    // Try to parse hostname as IP address first (skip DNS for IPs)
    std::error_code ip_parse_ec;
    auto ip_address = ::asio::ip::make_address(hostname, ip_parse_ec);

    ::asio::ip::tcp::endpoint direct_endpoint;
    bool is_ip_address = !ip_parse_ec;

    if (is_ip_address) {
        // Hostname is already an IP address, skip DNS resolution
        direct_endpoint = ::asio::ip::tcp::endpoint(ip_address, port);
        spdlog::trace("[async_connect] {} is an IP address, skipping DNS", hostname);
    } else {
        // Need DNS resolution
        auto resolver = std::make_shared<::asio::ip::tcp::resolver>(executor);

        // DNS resolution timeout (5 seconds should be plenty for DNS)
        auto dns_timer = std::make_shared<::asio::steady_timer>(executor);
        dns_timer->expires_after(std::chrono::seconds(5));

        auto start_time = std::chrono::steady_clock::now();
        spdlog::debug("[async_connect] Starting DNS resolution for {} with 5s timeout", hostname);

        // Use a channel to get the first result (DNS or timeout)
        auto result_channel = std::make_shared<concurrent_channel<
            std::variant<
                std::tuple<std::error_code, ::asio::ip::tcp::resolver::results_type>,
                std::error_code
            >
        >>(executor, 1);

        // Spawn DNS resolution
        ::asio::co_spawn(executor, [resolver, hostname, port, result_channel]() -> ::asio::awaitable<void> {
            auto [ec, endpoints] = co_await resolver->async_resolve(
                hostname, std::to_string(port), ::asio::as_tuple(::asio::use_awaitable));
            result_channel->try_send(std::error_code{},
                std::variant<std::tuple<std::error_code, ::asio::ip::tcp::resolver::results_type>, std::error_code>{
                    std::make_tuple(ec, endpoints)});
        }, ::asio::detached);

        // Spawn timer
        ::asio::co_spawn(executor, [dns_timer, result_channel]() -> ::asio::awaitable<void> {
            auto [ec] = co_await dns_timer->async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                result_channel->try_send(std::error_code{},
                    std::variant<std::tuple<std::error_code, ::asio::ip::tcp::resolver::results_type>, std::error_code>{
                        std::make_error_code(std::errc::timed_out)});
            }
        }, ::asio::detached);

        // Wait for first result
        auto [recv_ec, result] = co_await result_channel->async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        // Cancel whichever didn't complete
        resolver->cancel();
        dns_timer->cancel();

        if (recv_ec) {
            spdlog::debug("[async_connect] DNS channel error for {}: {}", hostname, recv_ec.message());
            co_return std::unexpected(error::resolve_failed);
        }

        // Check which result we got
        if (result.index() == 1) {
            // Timeout
            spdlog::debug("[async_connect] DNS resolution for {} timed out after {}ms", hostname, elapsed);
            co_return std::unexpected(error::resolve_failed);
        }

        auto [resolve_ec, endpoints] = std::get<0>(result);
        spdlog::debug("[async_connect] DNS completed for {} in {}ms, ec={}", hostname, elapsed, resolve_ec.message());

        if (resolve_ec) {
            spdlog::debug("[async_connect] Failed to resolve {}: {}", hostname, resolve_ec.message());
            co_return std::unexpected(error::resolve_failed);
        }

        // Use first resolved endpoint
        direct_endpoint = *endpoints.begin();
    }

    // Create socket and timer for connection timeout
    ::asio::ip::tcp::socket socket(executor);
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);

    // Race: connect vs timeout (use single endpoint connect)
    auto connect_result = co_await (
        socket.async_connect(direct_endpoint, ::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    if (connect_result.index() == 1) {
        // Timeout won
        spdlog::debug("[async_connect] Connection to {}:{} timed out", hostname, port);
        co_return std::unexpected(error::channel_timeout);
    }

    auto [connect_ec] = std::get<0>(connect_result);
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
