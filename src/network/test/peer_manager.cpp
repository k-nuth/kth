// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <future>
#include <thread>

#include <test_helpers.hpp>

#include <kth/network.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

// =============================================================================
// Test helpers
// =============================================================================

// Create a connected socket pair for testing (client, server)
std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>
make_connected_sockets_pm(::asio::io_context& ctx) {
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = acceptor.local_endpoint().port();

    ::asio::ip::tcp::socket client(ctx);
    ::asio::ip::tcp::socket server(ctx);

    std::thread accept_thread([&]() {
        server = acceptor.accept();
    });

    client.connect({::asio::ip::address_v4::loopback(), port});

    accept_thread.join();

    return {std::move(client), std::move(server)};
}

// Helper to run context with timeout and stop condition
template<typename Predicate>
void run_until_pm(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

// Create default settings for testing
network::settings make_test_settings_pm() {
    network::settings settings;
    settings.identifier = 0xe8f3e1e3;  // BCH testnet magic
    settings.protocol_maximum = 70015;
    settings.validate_checksum = true;
    settings.channel_inactivity_minutes = 1;
    settings.channel_expiration_minutes = 5;
    settings.inbound_port = 18333;
    return settings;
}

// Create a peer_session for testing
peer_session::ptr make_test_peer(::asio::io_context& ctx) {
    auto settings = make_test_settings_pm();
    auto [client, server] = make_connected_sockets_pm(ctx);
    return std::make_shared<peer_session>(std::move(server), settings);
}

// =============================================================================
// Construction Tests
// =============================================================================

TEST_CASE("peer_manager construction", "[peer_manager]") {
    ::asio::io_context ctx;

    peer_manager manager(ctx.get_executor(), 100);

    REQUIRE(!manager.stopped());
    REQUIRE(manager.count_snapshot() == 0);
}

TEST_CASE("peer_manager construction unlimited", "[peer_manager]") {
    ::asio::io_context ctx;

    peer_manager manager(ctx.get_executor(), 0);  // Unlimited

    REQUIRE(!manager.stopped());
}

// =============================================================================
// Add/Remove Tests
// =============================================================================

TEST_CASE("peer_manager add peer", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    code result = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await manager.add(peer);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(result == error::success);
    REQUIRE(manager.count_snapshot() == 1);
}

TEST_CASE("peer_manager add multiple peers", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 5; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<int> added{0};

    for (auto const& peer : peers) {
        ::asio::co_spawn(ctx, [&, peer]() -> ::asio::awaitable<void> {
            auto result = co_await manager.add(peer);
            if (result == error::success) {
                ++added;
            }
        }, ::asio::detached);
    }

    run_until_pm(ctx, [&]{ return added.load() >= 5; });

    REQUIRE(added == 5);
    REQUIRE(manager.count_snapshot() == 5);
}

TEST_CASE("peer_manager add duplicate nonce rejected", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer1 = make_test_peer(ctx);
    peer1->set_nonce(12345);

    auto peer2 = make_test_peer(ctx);
    peer2->set_nonce(12345);  // Same nonce

    std::atomic<bool> done{false};
    code result1 = error::unknown;
    code result2 = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result1 = co_await manager.add(peer1);
        result2 = co_await manager.add(peer2);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(result1 == error::success);
    REQUIRE(result2 == error::address_in_use);
    REQUIRE(manager.count_snapshot() == 1);
}

TEST_CASE("peer_manager add null peer rejected", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::atomic<bool> done{false};
    code result = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await manager.add(nullptr);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(result == error::operation_failed);
    REQUIRE(manager.count_snapshot() == 0);
}

TEST_CASE("peer_manager remove peer", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await manager.add(peer);
        REQUIRE(manager.count_snapshot() == 1);

        co_await manager.remove(peer);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(manager.count_snapshot() == 0);
}

TEST_CASE("peer_manager remove by nonce", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await manager.add(peer);
        REQUIRE(manager.count_snapshot() == 1);

        co_await manager.remove_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(manager.count_snapshot() == 0);
}

TEST_CASE("peer_manager remove nonexistent is safe", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::atomic<bool> done{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await manager.remove_by_nonce(99999);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(manager.count_snapshot() == 0);
}

// =============================================================================
// Capacity Tests
// =============================================================================

TEST_CASE("peer_manager capacity limit", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 3);  // Max 3 peers

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 5; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<int> success_count{0};
    std::atomic<int> rejected_count{0};
    std::atomic<int> done_count{0};

    for (auto const& peer : peers) {
        ::asio::co_spawn(ctx, [&, peer]() -> ::asio::awaitable<void> {
            auto result = co_await manager.add(peer);
            if (result == error::success) {
                ++success_count;
            } else if (result == error::channel_stopped) {
                ++rejected_count;
            }
            ++done_count;
        }, ::asio::detached);
    }

    run_until_pm(ctx, [&]{ return done_count.load() >= 5; });

    REQUIRE(success_count == 3);
    REQUIRE(rejected_count == 2);
    REQUIRE(manager.count_snapshot() == 3);
}

// =============================================================================
// Lookup Tests
// =============================================================================

TEST_CASE("peer_manager exists by nonce", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    bool exists_before = true;
    bool exists_after = false;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        exists_before = co_await manager.exists_by_nonce(12345);
        co_await manager.add(peer);
        exists_after = co_await manager.exists_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(!exists_before);
    REQUIRE(exists_after);
}

TEST_CASE("peer_manager find by nonce", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    peer_session::ptr found_before = nullptr;
    peer_session::ptr found_after = nullptr;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        found_before = co_await manager.find_by_nonce(12345);
        co_await manager.add(peer);
        found_after = co_await manager.find_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(found_before == nullptr);
    REQUIRE(found_after != nullptr);
    REQUIRE(found_after.get() == peer.get());
}

TEST_CASE("peer_manager count", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::atomic<bool> done{false};
    size_t count1 = 999;
    size_t count2 = 999;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        count1 = co_await manager.count();

        auto peer = make_test_peer(ctx);
        peer->set_nonce(12345);
        co_await manager.add(peer);

        count2 = co_await manager.count();
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(count1 == 0);
    REQUIRE(count2 == 1);
}

TEST_CASE("peer_manager all", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> added_peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        added_peers.push_back(peer);
    }

    std::atomic<bool> done{false};
    std::vector<peer_session::ptr> retrieved_peers;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : added_peers) {
            co_await manager.add(peer);
        }
        retrieved_peers = co_await manager.all();
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(retrieved_peers.size() == 3);
}

// =============================================================================
// for_each Tests
// =============================================================================

TEST_CASE("peer_manager for_each", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<bool> done{false};
    std::atomic<int> visited_count{0};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            co_await manager.add(peer);
        }

        co_await manager.for_each([&](peer_session::ptr) {
            ++visited_count;
        });

        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(visited_count == 3);
}

TEST_CASE("peer_manager for_each skips stopped peers", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    // Stop one peer
    peers[1]->stop();

    std::atomic<bool> done{false};
    std::atomic<int> visited_count{0};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            co_await manager.add(peer);
        }

        co_await manager.for_each([&](peer_session::ptr) {
            ++visited_count;
        });

        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(visited_count == 2);  // Only 2 active peers visited
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("peer_manager stop_all", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = make_test_peer(ctx);
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<bool> done{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            co_await manager.add(peer);
        }
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(!manager.stopped());
    REQUIRE(manager.count_snapshot() == 3);

    manager.stop_all();
    REQUIRE(manager.stopped());

    // Run io_context to process the posted stop operation
    // poll() processes all ready handlers without blocking
    ctx.restart();
    ctx.poll();

    REQUIRE(manager.count_snapshot() == 0);

    // All peers should be stopped
    for (auto const& peer : peers) {
        REQUIRE(peer->stopped());
    }
}

TEST_CASE("peer_manager stop_all is idempotent", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    manager.stop_all();
    REQUIRE(manager.stopped());

    manager.stop_all();  // Second call should be safe
    REQUIRE(manager.stopped());
}

TEST_CASE("peer_manager add after stop rejected", "[peer_manager]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    manager.stop_all();

    auto peer = make_test_peer(ctx);
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    code result = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await manager.add(peer);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });

    REQUIRE(result == error::service_stopped);
}

// =============================================================================
// Concurrent Access Tests
// =============================================================================

TEST_CASE("peer_manager concurrent add/remove", "[peer_manager][concurrent]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor(), 100);

    constexpr int num_operations = 50;
    std::atomic<int> completed{0};

    // Spawn many concurrent add operations
    for (int i = 0; i < num_operations; ++i) {
        ::asio::co_spawn(ctx, [&, i]() -> ::asio::awaitable<void> {
            auto peer = make_test_peer(ctx);
            peer->set_nonce(static_cast<uint64_t>(i));
            co_await manager.add(peer);

            // Immediately remove half
            if (i % 2 == 0) {
                co_await manager.remove(peer);
            }

            ++completed;
        }, ::asio::detached);
    }

    run_until_pm(ctx, [&]{ return completed.load() >= num_operations; }, 5000ms);

    REQUIRE(completed == num_operations);
    // Half should remain (odd numbered)
    REQUIRE(manager.count_snapshot() == num_operations / 2);
}

// =============================================================================
// Destructor Test
// =============================================================================

TEST_CASE("peer_manager destructor stops peers", "[peer_manager]") {
    ::asio::io_context ctx;
    std::vector<peer_session::ptr> peers;

    {
        peer_manager manager(ctx.get_executor(), 100);

        for (int i = 0; i < 3; ++i) {
            auto peer = make_test_peer(ctx);
            peer->set_nonce(1000 + i);
            peers.push_back(peer);
        }

        std::atomic<bool> done{false};

        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            for (auto const& peer : peers) {
                co_await manager.add(peer);
            }
            done = true;
        }, ::asio::detached);

        run_until_pm(ctx, [&]{ return done.load(); });

        // manager destructor called here
    }

    // Wait for stop to propagate (async post to strand from destructor)
    run_until_pm(ctx, [&]{
        for (auto const& peer : peers) {
            if (!peer->stopped()) return false;
        }
        return true;
    });

    // All peers should be stopped
    for (auto const& peer : peers) {
        REQUIRE(peer->stopped());
    }
}
