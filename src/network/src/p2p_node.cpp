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
    , pool_(thread_ceiling(settings.threads))  // 0 means use all cores
    , manager_(pool_.get_executor())
    , hosts_(settings)
    , top_block_({null_hash, 0})
    , new_peer_channel_(std::make_unique<concurrent_channel<peer_event>>(pool_.get_executor(), 100))
    , stop_signal_(std::make_unique<concurrent_event_channel>(pool_.get_executor(), 1))
{
    spdlog::debug("[p2p_node] p2p_node constructor - thread pool size: {}", pool_.size());
    // Register default message handlers using typed registration
    // The make_handler<> wrapper handles parsing automatically
    dispatcher_.register_handler<domain::message::ping>(handlers::ping::handle);
    dispatcher_.register_handler<domain::message::pong>(handlers::pong::handle);
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
    spdlog::debug("[p2p_node] run() starting");

    if (stopped_) {
        spdlog::debug("[p2p_node] run() - already stopped");
        co_return error::service_stopped;
    }

    // Run all network tasks in parallel using task_group on pool_ executor.
    // We use task_group instead of && operator because && runs all coroutines
    // on the caller's executor (which may be single-threaded). task_group
    // uses pool_.get_executor() which has multiple threads, allowing true
    // parallelism and proper coroutine scheduling.
    task_group network_tasks(pool_.get_executor());

    spdlog::debug("[p2p_node] Spawning peer_supervisor...");
    network_tasks.spawn([this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] peer_supervisor coroutine starting");
        co_await peer_supervisor();
        spdlog::debug("[p2p_node] peer_supervisor coroutine finished");
    });

    spdlog::debug("[p2p_node] Spawning run_inbound...");
    network_tasks.spawn([this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] run_inbound coroutine starting");
        co_await run_inbound();
        spdlog::debug("[p2p_node] run_inbound coroutine finished");
    });

    spdlog::debug("[p2p_node] Spawning run_outbound...");
    network_tasks.spawn([this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] run_outbound coroutine starting");
        co_await run_outbound();
        spdlog::debug("[p2p_node] run_outbound coroutine finished");
    });

    // Wait for supervisor to be ready before connecting manual peers
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped_) {
        co_await ::asio::post(pool_.get_executor(), ::asio::use_awaitable);
    }

    // Connect to configured manual peers (supervisor is now ready)
    if (!settings_.peers.empty()) {
        spdlog::debug("[p2p_node] run() - connecting to {} manual peers", settings_.peers.size());
        for (auto const& peer : settings_.peers) {
            auto result = co_await connect(peer.host(), peer.port());
            if (!result) {
                spdlog::warn("[p2p_node] Failed to connect to configured peer {}:{} - {}",
                    peer.host(), peer.port(), result.error().message());
            }
        }
    }

    spdlog::debug("[p2p_node] All tasks spawned, waiting on join...");
    // Wait for all tasks to complete (i.e., until stop() is called)
    co_await network_tasks.join();
    spdlog::debug("[p2p_node] All tasks completed");

    co_return error::success;
}

void p2p_node::stop() {
    if (stopped_) {
        return;
    }

    stopped_ = true;
    supervisor_ready_ = false;  // Reset for potential restart

    // Close stop_signal_ channel - this wakes up ALL waiters (peer_supervisor,
    // maintain_outbound_connections) because async_receive returns error when closed
    if (stop_signal_) {
        stop_signal_->close();
    }

    // Close the new peer channel to wake up peer_supervisor
    if (new_peer_channel_) {
        new_peer_channel_->close();
    }

    // Stop acceptor - this causes run_inbound() to exit
    if (acceptor_) {
        std::error_code ec;
        acceptor_->close(ec);
    }

    // Stop all peers - this causes peer->run() to exit, which allows
    // peer_supervisor's peer_tasks.join() to complete
    manager_.stop_all();

    // hosts_ saves to file in destructor

    // NOTE: We do NOT call pool_.stop() here!
    // With structured concurrency, all coroutines will complete naturally:
    // 1. run_inbound() exits (acceptor closed)
    // 2. run_outbound() exits (stopped_ = true)
    // 3. peer_supervisor() exits after peer_tasks.join() completes
    // 4. run() returns, then join() can complete
    // Calling pool_.stop() here would abort pending work and prevent clean shutdown.
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

    // Create response channel for handshake result
    auto response_channel = std::make_shared<handshake_response_channel>(executor, 1);

    // Send peer to supervisor for FULL lifecycle management (structured concurrency)
    // The supervisor will do: peer->run() && (handshake && protocols)
    // This eliminates the need for detached coroutines entirely!
    if (new_peer_channel_ && new_peer_channel_->is_open()) {
        co_await new_peer_channel_->async_send(
            std::error_code{},
            peer_event{peer, peer_direction::outbound, response_channel},
            ::asio::use_awaitable);
    } else {
        peer->stop();
        co_return std::unexpected(error::service_stopped);
    }

    // Wait for supervisor to complete handshake and send result
    auto [recv_ec, response] = co_await response_channel->async_receive(
        ::asio::as_tuple(::asio::use_awaitable));

    if (recv_ec || response.result != error::success) {
        // Handshake failed - peer is already stopped by supervisor
        auto err = recv_ec ? error::channel_stopped : response.result;
        spdlog::debug("[p2p_node] Handshake failed with {}:{} - {}",
            host, port, err.message());
        co_return std::unexpected(err);
    }

    spdlog::info("[p2p_node] Connection complete with {}:{}, version {}, {}",
        host, port, peer->negotiated_version(),
        peer->peer_version()->user_agent());

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

    if (settings_.seeds.empty()) {
        spdlog::info("[p2p_node] No seeds configured");
        co_return;
    }

    // Use pool's executor for parallel DNS resolution, not this_coro::executor
    // (which might be a single-threaded io_context from the caller)
    auto executor = pool_.get_executor();

    // Copy seeds to avoid reference issues in coroutines
    std::vector<infrastructure::config::endpoint> seeds_copy(
        settings_.seeds.begin(), settings_.seeds.end());

    // Use task_group for structured concurrency - no detached!
    task_group seed_tasks(executor);

    // Track seeds completed for early exit
    auto seeds_completed = std::make_shared<std::atomic<size_t>>(0);
    auto const total_seeds = seeds_copy.size();

    // Launch all seed connections in parallel
    for (auto const& seed : seeds_copy) {
        if (stopped_) break;

        seed_tasks.spawn([this, host = seed.host(), port = seed.port(), seeds_completed]() -> ::asio::awaitable<void> {
            co_await connect_to_seed(host, port, seeds_completed);
        });
    }

    spdlog::debug("[p2p_node] run_seeding: {} seed tasks spawned", seeds_copy.size());

    // Wait for seeds with early exit conditions
    // We use a timer loop to check for early exit while tasks run
    ::asio::steady_timer check_timer(executor);
    auto const max_wait = std::chrono::seconds(settings_.connect_timeout_seconds + 35);
    auto const start_time = std::chrono::steady_clock::now();

    while (seed_tasks.has_active_tasks() && !stopped_) {
        // Check elapsed time
        if (std::chrono::steady_clock::now() - start_time >= max_wait) {
            spdlog::debug("[p2p_node] Seeding timeout reached");
            break;
        }

        // Early exit if we have enough addresses
        if (hosts_.count() >= settings_.host_pool_capacity / 4) {
            spdlog::info("[p2p_node] Collected sufficient addresses, stopping seeding early");
            break;
        }

        // Wait a bit before checking again
        check_timer.expires_after(1s);
        co_await check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }

    // Wait for remaining tasks to complete (structured concurrency)
    co_await seed_tasks.join();

    spdlog::info("[p2p_node] Seeding complete, {} addresses available", hosts_.count());
}

::asio::awaitable<void> p2p_node::connect_to_seed(
    std::string seed_host,
    uint16_t seed_port,
    std::shared_ptr<std::atomic<size_t>> seeds_completed)
{
    spdlog::debug("[p2p_node] connect_to_seed: starting for {}:{}", seed_host, seed_port);

    // Use pool's executor explicitly for parallel operations
    auto executor = pool_.get_executor();

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

        // Run the seeding protocol using structured concurrency:
        // peer->run() && seeding_protocol()
        // This eliminates the need for detached coroutines!
        co_await (
            peer->run() &&
            [&]() -> ::asio::awaitable<void> {
                // Generate nonce for handshake
                uint64_t nonce = generate_nonce();

                // Perform handshake (uses channels - run() is running in parallel!)
                spdlog::debug("[p2p_node] connect_to_seed: performing handshake for {}:{}", seed_host, seed_port);
                auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
                auto handshake_result = co_await perform_handshake(*peer, config);

                if (!handshake_result) {
                    spdlog::debug("[p2p_node] Seed handshake failed {}:{} - {}",
                        seed_host, seed_port, handshake_result.error().message());
                    peer->stop();
                    co_return;
                }

                spdlog::debug("[p2p_node] connect_to_seed: handshake complete for {}:{}", seed_host, seed_port);

                // Send getaddr request
                auto ec = co_await peer->send(domain::message::get_address{});
                if (ec != error::success) {
                    peer->stop();
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
            }()
        );
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

    // Wait for peer_supervisor to be ready (deterministic synchronization)
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped_) {
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }

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

        // Check banlist before proceeding
        if (banlist_.is_banned(peer->authority())) {
            spdlog::debug("[p2p_node] Rejecting inbound connection from banned peer {}",
                peer->authority());
            peer->stop();
            continue;
        }

        // Send peer to supervisor for FULL lifecycle management (structured concurrency)
        // Supervisor will do: peer->run() && (handshake && add_to_manager && protocols)
        // No response channel needed for inbound - we don't wait for handshake result
        if (new_peer_channel_ && new_peer_channel_->is_open()) {
            co_await new_peer_channel_->async_send(
                std::error_code{},
                peer_event{peer, peer_direction::inbound, nullptr},
                ::asio::use_awaitable);
        } else {
            peer->stop();
        }
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

    // Wait for peer_supervisor to be ready (deterministic synchronization)
    // The && operator starts coroutines but doesn't guarantee execution order.
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped_) {
        // Yield to allow peer_supervisor to start
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }

    spdlog::debug("[p2p_node] maintain_outbound_connections started");

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

                // Use task_group for structured concurrency - no detached!
                task_group connection_tasks(executor);

                for (auto const& addr : addresses) {
                    auto const authority = infrastructure::config::authority(addr);
                    auto const ip = authority.asio_ip();

                    // Add to pending connections before spawning
                    pending_connections_.insert(ip);

                    connection_tasks.spawn([this, addr, ip]() -> ::asio::awaitable<void> {
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
                        }
                    });
                }

                // Wait for ALL connection attempts to complete (structured concurrency!)
                co_await connection_tasks.join();

                // Continue loop to check if we need more connections
                continue;
            }
        }

        // Wait before next check (short interval so shutdown is responsive)
        // NOTE: We don't use || with stop_signal_ because the || operator in asio
        // waits for BOTH operations to complete, not just the first one.
        timer.expires_after(std::chrono::seconds(5));
        co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        // Loop will exit on next iteration if stopped_ is true
    }
}

// =============================================================================
// Peer Supervisor (Structured Concurrency)
// =============================================================================
//
// The peer_supervisor manages all peer lifecycles using the nursery pattern.
// Instead of spawning detached coroutines for each peer, all peer tasks are
// tracked in a task_group and properly awaited on shutdown.
//
// Flow:
//   1. run_inbound() and connect() send peer_events to new_peer_channel_
//   2. peer_supervisor receives events and spawns peer tasks into task_group
//   3. On stop(), stop_signal_ is triggered
//   4. peer_supervisor stops accepting new peers and waits for all tasks to complete
//
// =============================================================================

::asio::awaitable<void> p2p_node::peer_supervisor() {
    spdlog::debug("[p2p_node] peer_supervisor started");

    task_group peer_tasks(pool_.get_executor());

    // Track ALL spawned peers (including those not yet in manager)
    // This is needed for clean shutdown - manager_.stop_all() only stops
    // peers that completed handshake, but we need to stop ALL peers.
    std::vector<peer_session::ptr> all_spawned_peers;

    // Signal that we're ready to receive peers (deterministic synchronization)
    supervisor_ready_.store(true, std::memory_order_release);

    while (!stopped_) {
        // Wait for new peer event
        // NOTE: We don't use || with stop_signal_ because the || operator in asio
        // waits for BOTH operations to complete, not just the first one.
        // Instead, stop() closes new_peer_channel_ which causes async_receive to return error.
        auto [ec, event] = co_await new_peer_channel_->async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            // Channel closed (stop() was called) or error - exit
            spdlog::debug("[p2p_node] peer_supervisor channel closed: {}", ec.message());
            break;
        }

        auto peer = event.peer;
        auto direction = event.direction;
        auto response_channel = event.response_channel;

        // Track this peer for shutdown
        all_spawned_peers.push_back(peer);

        spdlog::debug("[p2p_node] peer_supervisor received {} peer [{}]",
            direction == peer_direction::inbound ? "inbound" : "outbound",
            peer->authority());

        // Spawn FULL peer lifecycle into the task group (tracked, not detached!)
        // This runs: peer->run() && (handshake && add_to_manager && protocols)
        // The && operator ensures both branches run in parallel and we wait for both.
        // When peer disconnects, both exit and && completes.
        peer_tasks.spawn([this, peer, direction, response_channel]() -> ::asio::awaitable<void> {
            try {
                co_await (
                    peer->run() &&
                    [&]() -> ::asio::awaitable<void> {
                        // Generate nonce for handshake
                        uint64_t nonce = generate_nonce();
                        auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);

                        // Perform handshake (uses channels - run() is already running!)
                        auto handshake_result = co_await perform_handshake(*peer, config);

                        if (!handshake_result) {
                            spdlog::debug("[p2p_node] Handshake failed for [{}]: {}",
                                peer->authority(), handshake_result.error().message());
                            // Send failure response if caller is waiting
                            if (response_channel) {
                                response_channel->try_send(std::error_code{}, handshake_response{handshake_result.error()});
                            }
                            peer->stop();
                            co_return;
                        }

                        spdlog::info("[p2p_node] Handshake complete with [{}], version {}, {}",
                            peer->authority(), handshake_result->negotiated_version,
                            handshake_result->peer_version->user_agent());

                        // Add to peer manager
                        auto add_ec = co_await manager_.add(peer);
                        if (add_ec != error::success) {
                            spdlog::debug("[p2p_node] Failed to add peer [{}] to manager: {}",
                                peer->authority(), add_ec.message());
                            if (response_channel) {
                                response_channel->try_send(std::error_code{}, handshake_response{add_ec});
                            }
                            peer->stop();
                            co_return;
                        }

                        // Send initial ping to get latency data
                        {
                            uint64_t ping_nonce = 0;
                            pseudo_random::fill(reinterpret_cast<uint8_t*>(&ping_nonce), sizeof(ping_nonce));
                            domain::message::ping ping_msg(ping_nonce);
                            auto ec = co_await peer->send(ping_msg);
                            if (ec == error::success) {
                                peer->record_ping_sent(ping_nonce);
                            }
                        }

                        // Send success response to caller
                        if (response_channel) {
                            response_channel->try_send(std::error_code{}, handshake_response{error::success});
                        }

                        // Run protocols until peer disconnects
                        co_await run_peer_protocols(peer);
                    }()
                );
            } catch (std::exception const& e) {
                spdlog::debug("[p2p_node] Peer [{}] task exception: {}",
                    peer->authority(), e.what());
                // Send error response if caller is still waiting
                if (response_channel) {
                    response_channel->try_send(std::error_code{}, handshake_response{error::operation_failed});
                }
            }

            spdlog::debug("[p2p_node] Peer [{}] full lifecycle completed", peer->authority());
        });
    }

    spdlog::debug("[p2p_node] peer_supervisor stopping {} spawned peers before join",
        all_spawned_peers.size());

    // Stop ALL spawned peers (including those not yet in manager)
    // This ensures peers still in handshake phase are also stopped,
    // allowing their run() to exit and peer_tasks.join() to complete.
    for (auto& peer : all_spawned_peers) {
        peer->stop();
    }

    spdlog::debug("[p2p_node] peer_supervisor waiting for {} active peer tasks",
        peer_tasks.active_count());

    // CRITICAL: Wait for all peer tasks to complete
    // This is the structured concurrency guarantee - no orphaned tasks!
    co_await peer_tasks.join();

    spdlog::debug("[p2p_node] peer_supervisor finished - all peer tasks completed");
}

uint64_t p2p_node::generate_nonce() {
    uint64_t nonce = 0;
    pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
    return nonce;
}

} // namespace kth::network
