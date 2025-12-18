// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/peer_manager.hpp>

#include <algorithm>

#include <asio/co_spawn.hpp>
#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::network {

// =============================================================================
// Construction / Destruction
// =============================================================================

peer_manager::peer_manager(::asio::any_io_executor executor, size_t max_connections)
    : strand_(::asio::make_strand(executor))
    , max_connections_(max_connections)
{}

peer_manager::~peer_manager() {
    // Direct cleanup in destructor - no async dispatch needed
    // since no other operations can be in flight during destruction
    stopped_.store(true);
    for (auto& [nonce, peer] : peers_) {
        peer->stop();
    }
    peers_.clear();
    count_.store(0);
}

// =============================================================================
// Peer Management
// =============================================================================

::asio::awaitable<code> peer_manager::add(peer_session::ptr peer) {
    if (stopped()) {
        co_return error::service_stopped;
    }

    if (!peer) {
        co_return error::operation_failed;
    }

    auto nonce = peer->nonce();
    if (nonce == 0) {
        // Generate a temporary nonce based on address hash if not set
        // This allows adding peers before handshake completes
        auto const& auth = peer->authority();
        nonce = std::hash<std::string>{}(auth.to_string());
    }

    // Execute on strand
    auto result = co_await ::asio::co_spawn(strand_, [this, peer, nonce]() -> ::asio::awaitable<code> {
        // Check capacity
        if (max_connections_ > 0 && peers_.size() >= max_connections_) {
            co_return error::channel_stopped;  // At capacity
        }

        // Check for duplicate
        if (peers_.find(nonce) != peers_.end()) {
            co_return error::address_in_use;  // Duplicate nonce
        }

        // Check for duplicate authority
        auto const& new_auth = peer->authority();
        for (auto const& [existing_nonce, existing_peer] : peers_) {
            if (existing_peer->authority() == new_auth) {
                co_return error::address_in_use;  // Duplicate authority
            }
        }

        // Add peer
        peers_.emplace(nonce, peer);
        count_.store(peers_.size());

        spdlog::debug("[peer_manager] Added peer [{}], total: {}",
            peer->authority(), peers_.size());

        co_return error::success;
    }, ::asio::use_awaitable);

    co_return result;
}

::asio::awaitable<void> peer_manager::remove(peer_session::ptr peer) {
    if (!peer) {
        co_return;
    }

    auto nonce = peer->nonce();
    if (nonce == 0) {
        auto const& auth = peer->authority();
        nonce = std::hash<std::string>{}(auth.to_string());
    }

    co_await remove_by_nonce(nonce);
}

::asio::awaitable<void> peer_manager::remove_by_nonce(uint64_t nonce) {
    co_await ::asio::co_spawn(strand_, [this, nonce]() -> ::asio::awaitable<void> {
        auto it = peers_.find(nonce);
        if (it != peers_.end()) {
            spdlog::debug("[peer_manager] Removed peer [{}], remaining: {}",
                it->second->authority(), peers_.size() - 1);
            peers_.erase(it);
            count_.store(peers_.size());
        }
        co_return;
    }, ::asio::use_awaitable);
}

::asio::awaitable<bool> peer_manager::exists_by_nonce(uint64_t nonce) const {
    co_return co_await ::asio::co_spawn(strand_, [this, nonce]() -> ::asio::awaitable<bool> {
        co_return peers_.find(nonce) != peers_.end();
    }, ::asio::use_awaitable);
}

::asio::awaitable<bool> peer_manager::exists_by_authority(
    infrastructure::config::authority const& authority) const
{
    co_return co_await ::asio::co_spawn(strand_, [this, &authority]() -> ::asio::awaitable<bool> {
        for (auto const& [nonce, peer] : peers_) {
            if (peer->authority() == authority) {
                co_return true;
            }
        }
        co_return false;
    }, ::asio::use_awaitable);
}

::asio::awaitable<peer_session::ptr> peer_manager::find_by_nonce(uint64_t nonce) const {
    co_return co_await ::asio::co_spawn(strand_, [this, nonce]() -> ::asio::awaitable<peer_session::ptr> {
        auto it = peers_.find(nonce);
        if (it != peers_.end()) {
            co_return it->second;
        }
        co_return nullptr;
    }, ::asio::use_awaitable);
}

::asio::awaitable<std::vector<peer_session::ptr>> peer_manager::all() const {
    co_return co_await ::asio::co_spawn(strand_, [this]() -> ::asio::awaitable<std::vector<peer_session::ptr>> {
        std::vector<peer_session::ptr> result;
        result.reserve(peers_.size());
        for (auto const& [nonce, peer] : peers_) {
            result.push_back(peer);
        }
        co_return result;
    }, ::asio::use_awaitable);
}

::asio::awaitable<size_t> peer_manager::count() const {
    co_return co_await ::asio::co_spawn(strand_, [this]() -> ::asio::awaitable<size_t> {
        co_return peers_.size();
    }, ::asio::use_awaitable);
}

size_t peer_manager::count_snapshot() const {
    return count_.load();
}

// =============================================================================
// Broadcasting
// =============================================================================

::asio::awaitable<void> peer_manager::for_each(peer_handler handler) {
    auto peers = co_await all();
    for (auto const& peer : peers) {
        if (!peer->stopped()) {
            handler(peer);
        }
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

void peer_manager::stop_all() {
    if (stopped_.exchange(true)) {
        return;  // Already stopped
    }

    spdlog::debug("[peer_manager] Stopping all peers");

    // Post to strand to safely iterate and clean up
    // Note: Callers should run the io_context to ensure completion
    ::asio::post(strand_, [this]() {
        for (auto& [nonce, peer] : peers_) {
            peer->stop();
        }
        peers_.clear();
        count_.store(0);
    });
}

bool peer_manager::stopped() const {
    return stopped_.load();
}

} // namespace kth::network
