// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_MANAGER_HPP
#define KTH_NETWORK_PEER_MANAGER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

#include <asio/awaitable.hpp>
#include <asio/strand.hpp>

namespace kth::network {

// =============================================================================
// peer_manager: Modern coroutine-based peer connection manager
// =============================================================================
//
// Replaces the legacy pending<connector>/pending<channel> collections in p2p
// with a single unified collection. All operations are serialized through
// a strand, eliminating the need for explicit mutexes.
//
// Usage:
//   peer_manager manager(executor, settings);
//
//   // Add a connected peer
//   co_await manager.add(session);
//
//   // Broadcast to all peers
//   co_await manager.broadcast(message);
//
//   // Remove a peer
//   co_await manager.remove(session);
//
// =============================================================================

class KN_API peer_manager {
public:
    using ptr = std::shared_ptr<peer_manager>;
    using peer_handler = std::function<void(peer_session::ptr)>;

    /// Construct a peer manager
    /// @param executor The executor for async operations
    /// @param max_connections Maximum number of connections (0 = unlimited)
    explicit peer_manager(::asio::any_io_executor executor, size_t max_connections = 0);

    /// Destructor - stops all peers
    ~peer_manager();

    // -------------------------------------------------------------------------
    // Peer Management
    // -------------------------------------------------------------------------

    /// Add a peer to the manager
    /// @param peer The peer session to add
    /// @return success if added, error if duplicate or at capacity
    ::asio::awaitable<code> add(peer_session::ptr peer);

    /// Remove a peer from the manager
    /// @param peer The peer session to remove
    ::asio::awaitable<void> remove(peer_session::ptr peer);

    /// Remove a peer by nonce
    /// @param nonce The nonce of the peer to remove
    ::asio::awaitable<void> remove_by_nonce(uint64_t nonce);

    /// Check if a peer with the given nonce exists
    /// @param nonce The nonce to check
    /// @return true if exists
    ::asio::awaitable<bool> exists_by_nonce(uint64_t nonce) const;

    /// Check if a peer with the given authority exists
    /// @param authority The authority (ip:port) to check
    /// @return true if exists
    ::asio::awaitable<bool> exists_by_authority(infrastructure::config::authority const& authority) const;

    /// Find a peer by nonce
    /// @param nonce The nonce to find
    /// @return The peer session or nullptr if not found
    ::asio::awaitable<peer_session::ptr> find_by_nonce(uint64_t nonce) const;

    /// Get all connected peers (snapshot)
    /// @return Vector of peer sessions
    ::asio::awaitable<std::vector<peer_session::ptr>> all() const;

    /// Get the number of connected peers
    /// @return Count of peers
    ::asio::awaitable<size_t> count() const;

    // -------------------------------------------------------------------------
    // Synchronous accessors (for compatibility, use with care)
    // -------------------------------------------------------------------------

    /// Get the number of connected peers (non-awaitable)
    /// Thread-safe but may be slightly stale
    size_t count_snapshot() const;

    // -------------------------------------------------------------------------
    // Broadcasting
    // -------------------------------------------------------------------------

    /// Broadcast a message to all connected peers
    /// @param message The message to broadcast
    /// @return Number of peers the message was sent to
    template <typename Message>
    ::asio::awaitable<size_t> broadcast(Message const& message) {
        auto peers = co_await all();
        size_t sent = 0;

        for (auto const& peer : peers) {
            if (!peer->stopped()) {
                auto ec = co_await peer->send(message);
                if (!ec) {
                    ++sent;
                }
            }
        }

        co_return sent;
    }

    /// Execute a handler for each peer
    /// @param handler The handler to execute
    ::asio::awaitable<void> for_each(peer_handler handler);

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Stop all peers and clear the collection
    void stop_all();

    /// Check if the manager is stopped
    bool stopped() const;

private:
    // Strand for serializing access to peers_
    ::asio::strand<::asio::any_io_executor> strand_;

    // Peers indexed by nonce
    std::unordered_map<uint64_t, peer_session::ptr> peers_;

    // Configuration
    size_t const max_connections_;
    std::atomic<bool> stopped_{false};
    std::atomic<size_t> count_{0};  // For snapshot access
};

} // namespace kth::network

#endif // KTH_NETWORK_PEER_MANAGER_HPP
