// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_P2P_NODE_HPP
#define KTH_NETWORK_P2P_NODE_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <kth/network/define.hpp>
#include <kth/network/hosts.hpp>
#include <kth/network/peer_manager.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/settings.hpp>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>

namespace kth::network {

// =============================================================================
// Connection result (combines peer_session with handshake info)
// =============================================================================

struct connection_result {
    peer_session::ptr session;
    handshake_result handshake;
};

// =============================================================================
// P2P Node (main networking class)
// =============================================================================

class KN_API p2p_node {
public:
    using ptr = std::shared_ptr<p2p_node>;
    using address = domain::message::network_address;

    explicit p2p_node(settings const& settings);
    ~p2p_node();

    // Lifecycle
    // -------------------------------------------------------------------------

    /// Start the node (load hosts, seed if needed)
    ::asio::awaitable<code> start();

    /// Run the node (start accepting connections and connecting to peers)
    ::asio::awaitable<code> run();

    /// Stop the node
    void stop();

    /// Block until all work is complete
    void join();

    // Properties
    // -------------------------------------------------------------------------

    [[nodiscard]]
    settings const& network_settings() const;

    [[nodiscard]]
    bool stopped() const;

    [[nodiscard]]
    size_t connection_count() const;

    [[nodiscard]]
    infrastructure::config::checkpoint top_block() const;

    void set_top_block(infrastructure::config::checkpoint const& top);
    void set_top_block(infrastructure::config::checkpoint&& top);

    // Manual connections
    // -------------------------------------------------------------------------

    /// Connect to a specific peer and perform handshake
    ::asio::awaitable<std::expected<peer_session::ptr, code>> connect(
        std::string const& host,
        uint16_t port);

    // Host management
    // -------------------------------------------------------------------------

    [[nodiscard]]
    size_t address_count() const;

    code store(address const& addr);
    code fetch_address(address& out) const;
    code remove(address const& addr);

    // Broadcasting
    // -------------------------------------------------------------------------

    /// Broadcast a message to all connected peers
    template <typename Message>
    ::asio::awaitable<size_t> broadcast(Message const& message) {
        co_return co_await manager_.broadcast(message);
    }

    // Peer access
    // -------------------------------------------------------------------------

    /// Get the peer manager (for advanced use)
    [[nodiscard]]
    peer_manager& peers();

    /// Get a copy of all connected peer sessions
    [[nodiscard]]
    std::vector<peer_session::ptr> get_peers() const;

private:
    // Internal coroutines
    ::asio::awaitable<void> run_seeding();
    ::asio::awaitable<void> run_outbound();
    ::asio::awaitable<void> run_inbound();
    ::asio::awaitable<void> run_peer_protocols(peer_session::ptr peer);
    ::asio::awaitable<void> maintain_outbound_connections();

    uint64_t generate_nonce();

    settings const& settings_;
    ::asio::thread_pool pool_;
    peer_manager manager_;
    hosts hosts_;

    // Acceptor for inbound connections
    std::unique_ptr<::asio::ip::tcp::acceptor> acceptor_;

    std::atomic<bool> stopped_{true};
    std::atomic<bool> seeded_{false};
    kth::atomic<infrastructure::config::checkpoint> top_block_;
};

} // namespace kth::network

#endif // KTH_NETWORK_P2P_NODE_HPP
