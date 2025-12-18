// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/p2p_node.hpp>

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
{}

p2p_node::~p2p_node() {
    stop();
    join();
}

::asio::awaitable<code> p2p_node::start() {
    if (!stopped_) {
        co_return error::operation_failed;
    }

    stopped_ = false;

    // Load hosts from file
    auto ec = hosts_.start();
    if (ec) {
        spdlog::error("[p2p_node] Failed to load hosts: {}", ec.message());
        co_return ec;
    }

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

    // Save hosts
    hosts_.stop();

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

::asio::awaitable<std::expected<peer_session::ptr, code>> p2p_node::connect(
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

    spdlog::info("[p2p_node] Handshake complete with {}:{}, version {}",
        host, port, handshake_result->negotiated_version);

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

code p2p_node::store(address const& addr) {
    return hosts_.store(addr);
}

code p2p_node::fetch_address(address& out) const {
    return hosts_.fetch(out);
}

code p2p_node::remove(address const& addr) {
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

    auto executor = co_await ::asio::this_coro::executor;

    for (auto const& seed : settings_.seeds) {
        if (stopped_) break;

        spdlog::debug("[p2p_node] Connecting to seed {}:{}", seed.host(), seed.port());

        // Use async_connect from peer_session.hpp
        auto result = co_await async_connect(
            executor,
            seed.host(),
            seed.port(),
            settings_,
            std::chrono::seconds(settings_.connect_timeout_seconds));

        if (!result) {
            spdlog::debug("[p2p_node] Failed to connect to seed {}:{} - {}",
                seed.host(), seed.port(), result.error().message());
            continue;
        }

        auto peer = *result;

        // Start the session's message pump
        ::asio::co_spawn(executor, peer->run(), ::asio::detached);

        // Generate nonce for handshake
        uint64_t nonce = generate_nonce();

        // Perform handshake
        auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
        auto handshake_result = co_await perform_handshake(*peer, config);

        if (!handshake_result) {
            peer->stop();
            spdlog::debug("[p2p_node] Seed handshake failed {}:{} - {}",
                seed.host(), seed.port(), handshake_result.error().message());
            continue;
        }

        // Request addresses
        auto addr_result = co_await request_addresses(*peer, 30s);

        if (addr_result) {
            spdlog::info("[p2p_node] Got {} addresses from seed {}:{}",
                addr_result->addresses().size(), seed.host(), seed.port());

            for (auto const& addr : addr_result->addresses()) {
                hosts_.store(addr);
            }
        }

        // Disconnect from seed
        peer->stop();
    }

    spdlog::info("[p2p_node] Seeding complete, {} addresses available", hosts_.count());
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

            spdlog::info("[p2p_node] Inbound handshake complete from {}, version {}",
                peer->authority(), handshake_result->negotiated_version);

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
    // Run ping/pong to keep connection alive
    co_await run_ping_pong(*peer, std::chrono::seconds(settings_.channel_heartbeat_minutes * 60));
}

::asio::awaitable<void> p2p_node::maintain_outbound_connections() {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    while (!stopped_) {
        auto const current_count = manager_.count_snapshot();
        auto const target = settings_.outbound_connections;

        if (current_count < target) {
            // Need more connections
            auto const needed = target - current_count;

            for (size_t i = 0; i < needed && !stopped_; ++i) {
                domain::message::network_address addr;
                auto ec = hosts_.fetch(addr);

                if (ec) {
                    spdlog::debug("[p2p_node] No addresses available for outbound");
                    break;
                }

                auto const authority = infrastructure::config::authority(addr);
                auto result = co_await connect(authority.to_hostname(), authority.port());

                if (!result) {
                    spdlog::debug("[p2p_node] Failed to connect to {}: {}",
                        authority, result.error().message());
                    hosts_.remove(addr);
                }
            }
        }

        // Wait before next check
        timer.expires_after(std::chrono::seconds(settings_.connect_batch_size > 0 ? 5 : 30));
        co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }
}

uint64_t p2p_node::generate_nonce() {
    uint64_t nonce = 0;
    pseudo_random::fill(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
    return nonce;
}

} // namespace kth::network
