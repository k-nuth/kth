// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/p2p_node.hpp>

#include <kth/network/handlers/ping.hpp>
#include <kth/network/handlers/pong.hpp>

#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::network {

using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// =============================================================================
// P2P Node implementation
// =============================================================================

p2p_node::p2p_node(settings const& settings)
    : settings_(settings)
    , pool_(settings.threads > 0 ? settings.threads : 1)
    , manager_(pool_.get_executor())
    , hosts_(settings)
    , top_block_({null_hash, 0})
{
    spdlog::debug("[p2p_node] p2p_node constructor - member init complete");
    // Register default message handlers using typed registration
    // The make_handler<> wrapper handles parsing automatically
    dispatcher_.register_handler<domain::message::ping>(handlers::ping::handle);
    dispatcher_.register_handler<domain::message::pong>(handlers::pong::handle);
    spdlog::debug("[p2p_node] p2p_node constructor completed successfully");
}

p2p_node::~p2p_node() {
    stop();
    join();
}

::asio::awaitable<code> p2p_node::start() {
    if (!stopped_) {
        co_return error::operation_failed;
    }

    stopped_ = false;

    // hosts_ loads from file in constructor
    spdlog::info("[p2p_node] Loaded {} host addresses", hosts_.count());

    // Seed if needed
    if (hosts_.count() < settings_.host_pool_capacity / 2) {
        co_await run_seeding();
    }

    seeded_ = true;
    co_return error::success;
}

::asio::awaitable<code> p2p_node::run() {
    if (stopped_) {
        co_return error::service_stopped;
    }

    // Start inbound acceptor
    ::asio::co_spawn(pool_.get_executor(), run_inbound(), ::asio::detached);

    // Start outbound connection manager
    ::asio::co_spawn(pool_.get_executor(), run_outbound(), ::asio::detached);

    // Connect to configured manual peers
    for (auto const& peer : settings_.peers) {
        auto result = co_await connect(peer.host(), peer.port());
        if (!result) {
            spdlog::warn("[p2p_node] Failed to connect to configured peer {}:{} - {}",
                peer.host(), peer.port(), result.error().message());
        }
    }

    co_return error::success;
}

void p2p_node::stop() {
    if (stopped_) {
        return;
    }

    stopped_ = true;

    // Stop acceptor
    if (acceptor_) {
        std::error_code ec;
        acceptor_->close(ec);
    }

    // Stop all peers
    manager_.stop_all();

    // hosts_ saves to file in destructor

    // Stop thread pool
    pool_.stop();
}

void p2p_node::join() {
    pool_.join();
}

// Properties
// -----------------------------------------------------------------------------

settings const& p2p_node::network_settings() const {
    return settings_;
}

bool p2p_node::stopped() const {
    return stopped_;
}

size_t p2p_node::connection_count() const {
    return manager_.count_snapshot();
}

threadpool& p2p_node::thread_pool() {
    return pool_;
}

message_dispatcher& p2p_node::dispatcher() {
    return dispatcher_;
}

banlist& p2p_node::bans() {
    return banlist_;
}

banlist const& p2p_node::bans() const {
    return banlist_;
}

void p2p_node::ban_peer(
    peer_session::ptr const& peer,
    std::chrono::seconds duration,
    ban_reason reason)
{
    if (peer) {
        banlist_.ban(peer->authority(), duration, reason);
        peer->stop(error::channel_stopped);
    }
}

bool p2p_node::is_banned(infrastructure::config::authority const& authority) const {
    return banlist_.is_banned(authority);
}

// =============================================================================
// Message Dispatcher implementation
// =============================================================================

::asio::awaitable<bool> message_dispatcher::dispatch(peer_session& peer, raw_message const& msg) {
    auto const& command = msg.heading.command();

    spdlog::debug("[dispatcher] Dispatching '{}' from [{}]", command, peer.authority_with_agent());

    // Look for specific handler
    auto it = handlers_.find(command);
    if (it != handlers_.end()) {
        auto result = co_await it->second(peer, msg);
        switch (result) {
            case message_result::disconnect:
                co_return false;
            case message_result::continue_processing:
            case message_result::handled:
                co_return true;
            case message_result::not_handled:
                // Fall through to default handler
                break;
        }
    }

    // Try default handler
    if (default_handler_) {
        auto result = co_await default_handler_(peer, msg);
        co_return result != message_result::disconnect;
    }

    // No handler - just continue
    spdlog::trace("[dispatcher] Unhandled message '{}' from [{}]", command, peer.authority_with_agent());
    co_return true;
}

void message_dispatcher::register_handler(std::string const& command, message_handler_fn handler) {
    handlers_[command] = std::move(handler);
}

void message_dispatcher::set_default_handler(message_handler_fn handler) {
    default_handler_ = std::move(handler);
}

infrastructure::config::checkpoint p2p_node::top_block() const {
    return top_block_.load();
}

void p2p_node::set_top_block(infrastructure::config::checkpoint const& top) {
    top_block_.store(top);
}

void p2p_node::set_top_block(infrastructure::config::checkpoint&& top) {
    top_block_.store(std::move(top));
}

// Manual connections
// -----------------------------------------------------------------------------

awaitable_expected<peer_session::ptr> p2p_node::connect(
    std::string const& host,
    uint16_t port)
{
    if (stopped_) {
        co_return std::unexpected(error::service_stopped);
    }

    auto executor = co_await ::asio::this_coro::executor;

    // Use async_connect from peer_session.hpp
    auto result = co_await async_connect(
        executor,
        host,
        port,
        settings_,
        std::chrono::seconds(settings_.connect_timeout_seconds));

    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto peer = *result;
    auto const ip = peer->authority().asio_ip();

    // Check if already connected to this IP
    if (co_await manager_.exists_by_ip(ip)) {
        spdlog::debug("[p2p_node] Already connected to IP {}, skipping", ip.to_string());
        peer->stop();
        co_return std::unexpected(error::address_in_use);
    }

    // Check banlist before proceeding
    if (banlist_.is_banned(peer->authority())) {
        spdlog::debug("[p2p_node] Rejecting connection to banned peer {}:{}", host, port);
        peer->stop();
        co_return std::unexpected(error::address_blocked);
    }

    // Start the session's message pump
    ::asio::co_spawn(executor, peer->run(), ::asio::detached);

    // Generate nonce for handshake
    uint64_t nonce = generate_nonce();

    // Perform handshake
    auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
    auto handshake_result = co_await perform_handshake(*peer, config);

    if (!handshake_result) {
        peer->stop();
        spdlog::debug("[p2p_node] Handshake failed with {}:{} - {}",
            host, port, handshake_result.error().message());
        co_return std::unexpected(handshake_result.error());
    }

    spdlog::info("[p2p_node] Handshake complete with {}:{}, version {}, {}",
        host, port, handshake_result->negotiated_version,
        handshake_result->peer_version->user_agent());

    // Send initial ping immediately to get latency data quickly (like BCHN)
    {
        uint64_t nonce = 0;
        pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
        domain::message::ping ping_msg(nonce);
        auto ec = co_await peer->send(ping_msg);
        if (ec == error::success) {
            peer->record_ping_sent(nonce);
        }
    }

    // Add to peer manager
    auto ec = co_await manager_.add(peer);
    if (ec != error::success) {
        peer->stop();
        co_return std::unexpected(ec);
    }

    // Start protocol handlers
    ::asio::co_spawn(executor, run_peer_protocols(peer), ::asio::detached);

    co_return peer;
}

// Host management
// -----------------------------------------------------------------------------

size_t p2p_node::address_count() const {
    return hosts_.count();
}

bool p2p_node::store(address const& addr) {
    return hosts_.store(addr);
}

code p2p_node::fetch_address(address& out) const {
    return hosts_.fetch(out);
}

bool p2p_node::remove(address const& addr) {
    return hosts_.remove(addr);
}

// Peer access
// -----------------------------------------------------------------------------

peer_manager& p2p_node::peers() {
    return manager_;
}

std::vector<peer_session::ptr> p2p_node::get_peers() const {
    // Note: This is a blocking call that runs the coroutine synchronously.
    // For async access, use peers().all() directly.
    std::vector<peer_session::ptr> result;
    // For now, return empty - callers should use the async version
    // TODO: Consider removing this sync method or implementing properly
    return result;
}

// Internal coroutines
// -----------------------------------------------------------------------------

::asio::awaitable<void> p2p_node::run_seeding() {
    spdlog::info("[p2p_node] Starting seeding from {} seeds", settings_.seeds.size());
    spdlog::debug("[p2p_node] run_seeding: before seeds check");

    if (settings_.seeds.empty()) {
        spdlog::info("[p2p_node] No seeds configured");
        co_return;
    }

    spdlog::debug("[p2p_node] run_seeding: getting executor");
    auto executor = co_await ::asio::this_coro::executor;
    spdlog::debug("[p2p_node] run_seeding: got executor");

    // Track seeds completed
    auto seeds_completed = std::make_shared<std::atomic<size_t>>(0);
    auto const total_seeds = settings_.seeds.size();
    spdlog::debug("[p2p_node] run_seeding: total_seeds = {}", total_seeds);

    // Copy seeds to avoid reference issues in coroutines
    spdlog::debug("[p2p_node] run_seeding: copying seeds");
    std::vector<infrastructure::config::endpoint> seeds_copy(
        settings_.seeds.begin(), settings_.seeds.end());
    spdlog::debug("[p2p_node] run_seeding: seeds copied, count = {}", seeds_copy.size());

    // Launch seed connections in parallel
    for (size_t i = 0; i < seeds_copy.size(); ++i) {
        if (stopped_) break;

        auto const& seed = seeds_copy[i];
        spdlog::debug("[p2p_node] run_seeding: processing seed {} of {}", i + 1, seeds_copy.size());

        auto seed_host = seed.host();
        auto seed_port = seed.port();
        spdlog::debug("[p2p_node] run_seeding: seed {}:{}", seed_host, seed_port);

        // Use member function instead of lambda to avoid capture lifetime issues
        spdlog::debug("[p2p_node] run_seeding: about to co_spawn connect_to_seed for {}:{}", seed_host, seed_port);
        ::asio::co_spawn(executor,
            connect_to_seed(std::move(seed_host), seed_port, seeds_completed),
            ::asio::detached);
        spdlog::debug("[p2p_node] run_seeding: co_spawned connect_to_seed");
    }

    spdlog::debug("[p2p_node] run_seeding: all seed tasks spawned, entering wait loop");

    // Wait for seeds with simple timeout
    auto const max_wait = std::chrono::seconds(settings_.connect_timeout_seconds + 35);
    auto const start_time = std::chrono::steady_clock::now();

    while (*seeds_completed < total_seeds && !stopped_) {
        // Check elapsed time
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= max_wait) {
            spdlog::debug("[p2p_node] Seeding timeout reached");
            break;
        }

        // Early exit if we have enough addresses
        if (hosts_.count() >= settings_.host_pool_capacity / 4) {
            spdlog::info("[p2p_node] Collected sufficient addresses, stopping seeding early");
            break;
        }

        // Wait a bit before checking again
        ::asio::steady_timer check_timer(executor);
        check_timer.expires_after(1s);
        co_await check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }

    spdlog::info("[p2p_node] Seeding complete, {} addresses available", hosts_.count());
}

::asio::awaitable<void> p2p_node::connect_to_seed(
    std::string seed_host,
    uint16_t seed_port,
    std::shared_ptr<std::atomic<size_t>> seeds_completed)
{
    spdlog::debug("[p2p_node] connect_to_seed: starting for {}:{}", seed_host, seed_port);

    auto executor = co_await ::asio::this_coro::executor;

    try {
        auto result = co_await async_connect(
            executor,
            seed_host,
            seed_port,
            settings_,
            std::chrono::seconds(settings_.connect_timeout_seconds));

        if (!result) {
            spdlog::debug("[p2p_node] Failed to connect to seed {}:{} - {}",
                seed_host, seed_port, result.error().message());
            ++(*seeds_completed);
            co_return;
        }

        spdlog::debug("[p2p_node] connect_to_seed: connected to {}:{}", seed_host, seed_port);
        auto peer = *result;

        // Start the session's message pump
        spdlog::debug("[p2p_node] connect_to_seed: spawning peer->run() for {}:{}", seed_host, seed_port);
        ::asio::co_spawn(executor, peer->run(), ::asio::detached);

        // Generate nonce for handshake
        uint64_t nonce = generate_nonce();

        // Perform handshake
        spdlog::debug("[p2p_node] connect_to_seed: performing handshake for {}:{}", seed_host, seed_port);
        auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
        auto handshake_result = co_await perform_handshake(*peer, config);

        if (!handshake_result) {
            peer->stop();
            spdlog::debug("[p2p_node] Seed handshake failed {}:{} - {}",
                seed_host, seed_port, handshake_result.error().message());
            ++(*seeds_completed);
            co_return;
        }

        spdlog::debug("[p2p_node] connect_to_seed: handshake complete for {}:{}", seed_host, seed_port);

        // Send getaddr request
        auto ec = co_await peer->send(domain::message::get_address{});
        if (ec != error::success) {
            peer->stop();
            ++(*seeds_completed);
            co_return;
        }

        // Wait for addr response with timeout
        ::asio::steady_timer timer(executor);
        timer.expires_after(30s);
        bool got_addresses = false;

        while (!got_addresses && !peer->stopped()) {
            auto msg_result = co_await (
                peer->messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
            );

            if (msg_result.index() == 1) {
                // Timeout
                break;
            }

            auto& [recv_ec, raw] = std::get<0>(msg_result);
            if (recv_ec) {
                break;
            }

            if (raw.heading.command() == domain::message::address::command) {
                byte_reader reader(raw.payload);
                auto addr_result = domain::message::address::from_data(
                    reader, peer->negotiated_version());
                if (addr_result) {
                    auto const count = addr_result->addresses().size();
                    spdlog::info("[p2p_node] Got {} addresses from seed {}:{}",
                        count, seed_host, seed_port);

                    for (auto const& addr : addr_result->addresses()) {
                        hosts_.store(addr);
                    }
                    got_addresses = true;
                }
            }
        }

        peer->stop();
    } catch (std::exception const& e) {
        spdlog::debug("[p2p_node] Seed {} exception: {}", seed_host, e.what());
    }

    ++(*seeds_completed);
}

::asio::awaitable<void> p2p_node::run_outbound() {
    co_await maintain_outbound_connections();
}

::asio::awaitable<void> p2p_node::run_inbound() {
    if (settings_.inbound_port == 0) {
        spdlog::info("[p2p_node] Inbound connections disabled (port 0)");
        co_return;
    }

    auto executor = co_await ::asio::this_coro::executor;

    // Use async_listen from peer_session.hpp
    auto listen_result = co_await async_listen(executor, settings_.inbound_port);
    if (!listen_result) {
        spdlog::error("[p2p_node] Failed to start listening: {}", listen_result.error().message());
        co_return;
    }

    acceptor_ = std::make_unique<::asio::ip::tcp::acceptor>(std::move(*listen_result));

    spdlog::info("[p2p_node] Listening on port {}", settings_.inbound_port);

    // Accept loop
    while (!stopped_) {
        // Use async_accept from peer_session.hpp
        auto result = co_await async_accept(*acceptor_, settings_);

        if (!result) {
            if (result.error() == error::service_stopped) {
                break;
            }
            spdlog::debug("[p2p_node] Accept error: {}", result.error().message());
            continue;
        }

        auto peer = *result;

        // Handle the new connection in a separate coroutine
        ::asio::co_spawn(executor, [this, peer, executor]() -> ::asio::awaitable<void> {
            // Check banlist before proceeding
            if (banlist_.is_banned(peer->authority())) {
                spdlog::debug("[p2p_node] Rejecting inbound connection from banned peer {}",
                    peer->authority());
                peer->stop();
                co_return;
            }

            // Start the session's message pump
            ::asio::co_spawn(executor, peer->run(), ::asio::detached);

            // Generate nonce for handshake
            uint64_t nonce = generate_nonce();

            // Perform handshake
            auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
            auto handshake_result = co_await perform_handshake(*peer, config);

            if (!handshake_result) {
                peer->stop();
                spdlog::debug("[p2p_node] Inbound handshake failed: {}",
                    handshake_result.error().message());
                co_return;
            }

            spdlog::info("[p2p_node] Inbound handshake complete from {}, version {}, {}",
                peer->authority(), handshake_result->negotiated_version,
                handshake_result->peer_version->user_agent());

            // Send initial ping immediately to get latency data quickly (like BCHN)
            {
                uint64_t nonce = 0;
                pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
                domain::message::ping ping_msg(nonce);
                auto ec = co_await peer->send(ping_msg);
                if (ec == error::success) {
                    peer->record_ping_sent(nonce);
                }
            }

            // Add to peer manager
            auto ec = co_await manager_.add(peer);
            if (ec != error::success) {
                peer->stop();
                co_return;
            }

            // Start protocol handlers
            co_await run_peer_protocols(peer);
        }, ::asio::detached);
    }
}

::asio::awaitable<void> p2p_node::run_peer_protocols(peer_session::ptr peer) {
    spdlog::debug("[p2p_node] Starting protocols for peer [{}]", peer->authority());

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer ping_timer(executor);
    auto const ping_interval = std::chrono::seconds(settings_.channel_heartbeat_minutes * 60);

    spdlog::debug("[p2p_node] Ping interval for [{}]: {}s", peer->authority(), ping_interval.count());

    while (!peer->stopped() && !stopped_) {
        ping_timer.expires_after(ping_interval);

        // Wait for message or ping timer
        auto result = co_await (
            peer->messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            ping_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        if (peer->stopped() || stopped_) {
            break;
        }

        if (result.index() == 1) {
            // Timer expired - send ping
            auto [timer_ec] = std::get<1>(result);
            if (!timer_ec) {
                uint64_t nonce = 0;
                pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
                domain::message::ping ping_msg(nonce);

                auto ec = co_await peer->send(ping_msg);
                if (ec != error::success) {
                    spdlog::debug("[p2p_node] Failed to send ping to [{}]", peer->authority());
                    break;
                }
                peer->record_ping_sent(nonce);
                spdlog::trace("[p2p_node] Sent ping to [{}]", peer->authority());
            }
            continue;
        }

        // Message received
        auto& [ec, raw] = std::get<0>(result);
        if (ec) {
            break;
        }

        auto const& command = raw.heading.command();

        // Route response messages to their dedicated channels
        // These are consumed by request_headers, request_block, request_addresses
        // Using try_send - if channel is full or no one is waiting, message is dropped
        // (this is correct behavior: unsolicited responses are ignored)
        if (command == domain::message::headers::command) {
            auto [send_ec] = co_await peer->headers_responses().async_send(
                std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
            if (!send_ec) {
                continue;  // Message delivered to response channel
            }
            // Channel full or closed - fall through to dispatcher
        } else if (command == domain::message::block::command) {
            auto [send_ec] = co_await peer->block_responses().async_send(
                std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
            if (!send_ec) {
                continue;
            }
        } else if (command == domain::message::address::command) {
            auto [send_ec] = co_await peer->addr_responses().async_send(
                std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
            if (!send_ec) {
                continue;
            }
        }

        // Dispatch message through handlers
        bool should_continue = co_await dispatcher_.dispatch(*peer, raw);
        if (!should_continue) {
            spdlog::debug("[p2p_node] Handler requested disconnect for [{}]", peer->authority());
            break;
        }
    }

    // Cleanup
    spdlog::debug("[p2p_node] Ending protocols for peer [{}]", peer->authority());
    peer->stop();
    co_await manager_.remove(peer);
}

::asio::awaitable<void> p2p_node::maintain_outbound_connections() {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    // Maximum number of parallel connection attempts
    constexpr size_t max_parallel_attempts = 8;

    while (!stopped_) {
        auto const current_count = manager_.count_snapshot();
        auto const target = settings_.outbound_connections;

        if (current_count < target) {
            // Need more connections
            auto const needed = target - current_count;

            // Always try max_parallel_attempts to find working peers faster
            // Many addresses may be stale, so cast a wide net
            auto const batch_size = max_parallel_attempts;
            std::vector<domain::message::network_address> addresses;
            addresses.reserve(batch_size);

            // Fetch addresses, filtering out duplicates and already-connected IPs
            for (size_t attempts = 0; attempts < batch_size * 3 && addresses.size() < batch_size && !stopped_; ++attempts) {
                domain::message::network_address addr;
                auto ec = hosts_.fetch(addr);
                if (ec) {
                    break;
                }

                auto const authority = infrastructure::config::authority(addr);
                auto const ip = authority.asio_ip();

                // Skip if already pending connection to this IP
                if (pending_connections_.contains(ip)) {
                    continue;
                }

                // Skip if already connected to this IP
                if (co_await manager_.exists_by_ip(ip)) {
                    continue;
                }

                // Skip if banned
                if (banlist_.is_banned(ip)) {
                    continue;
                }

                // Check for duplicate IP in current batch
                bool duplicate = false;
                for (auto const& existing : addresses) {
                    if (infrastructure::config::authority(existing).asio_ip() == ip) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;
                }

                addresses.push_back(addr);
            }

            if (addresses.empty()) {
                spdlog::debug("[p2p_node] No addresses available for outbound");
            } else {
                spdlog::debug("[p2p_node] Attempting {} parallel connections (need {}, have {})",
                    addresses.size(), needed, current_count);

                // Track successful connections with a shared atomic counter
                auto success_count = std::make_shared<std::atomic<size_t>>(0);

                for (auto const& addr : addresses) {
                    auto const authority = infrastructure::config::authority(addr);
                    auto const ip = authority.asio_ip();

                    // Add to pending connections before spawning
                    pending_connections_.insert(ip);

                    auto task = [this, addr, ip, success_count]() -> ::asio::awaitable<void> {
                        auto const authority = infrastructure::config::authority(addr);
                        spdlog::trace("[p2p_node] Attempting connection to {}", authority);

                        auto result = co_await connect(authority.to_hostname(), authority.port());

                        // Remove from pending connections when done
                        pending_connections_.erase(ip);

                        if (!result) {
                            spdlog::trace("[p2p_node] Connection failed to {}: {}",
                                authority, result.error().message());
                            hosts_.remove(addr);
                        } else {
                            spdlog::debug("[p2p_node] Connected to {}", authority);
                            ++(*success_count);
                        }
                    };

                    // Spawn each connection attempt
                    ::asio::co_spawn(executor, task(), ::asio::detached);
                }

                // Give connection attempts time to complete
                // Use a shorter wait since they're running in parallel
                timer.expires_after(std::chrono::seconds(settings_.connect_timeout_seconds + 2));
                co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));

                // Continue loop to check if we need more connections
                continue;
            }
        }

        // Wait before next check when we have enough connections
        timer.expires_after(std::chrono::seconds(30));
        co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }
}

uint64_t p2p_node::generate_nonce() {
    uint64_t nonce = 0;
    pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
    return nonce;
}

} // namespace kth::network
