// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

// Helper to run context with timeout and stop condition
template<typename Predicate>
void run_until_pm(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

// Every admission a case relies on, checked: a session the manager refused
// would leave the rest of the case testing something other than it says.
void require_admitted(std::vector<code> const& admissions, size_t expected) {
    REQUIRE(admissions.size() == expected);
    for (size_t i = 0; i < admissions.size(); ++i) {
        INFO("admission " << i);
        CHECK(admissions[i] == error::success);
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

// Sessions for the manager, each on the server end of a loopback connection.
//
// The manager tells peers apart by nonce and by authority, and a session's
// authority is its remote endpoint: here, the address of the client end. For
// every session to be a different peer, no two may share it. A listener per
// session does not ensure that: a source port can be reused towards a
// different destination, so a later session can come up with an earlier one's
// endpoint, and the manager, as it should, refuses it as a duplicate.
//
// So every session connects to the same listener, and every client end stays
// open until the case is over: the kernel never hands out a live connection's
// endpoints twice, and with our end fixed, the far ends are all different. A
// client end closed as soon as its session exists would no longer be live, and
// whether its port could come back would be up to the kernel's TIME_WAIT reuse.
struct test_peers {
    explicit test_peers(::asio::io_context& ctx)
        : ctx_(ctx)
        , listener_(ctx, {::asio::ip::address_v4::loopback(), 0})
    {}

    peer_session::ptr make() {
        ::asio::ip::tcp::socket client(ctx_);
        client.connect(listener_.local_endpoint());
        // The connection is already in the listener's backlog: nothing to
        // wait for on another thread.
        auto server = listener_.accept();
        clients_.push_back(std::move(client));
        return std::make_shared<peer_session>(std::move(server), make_test_settings_pm());
    }

    test_peers(test_peers const&) = delete;
    test_peers& operator=(test_peers const&) = delete;

private:
    ::asio::io_context& ctx_;
    ::asio::ip::tcp::acceptor listener_;
    std::vector<::asio::ip::tcp::socket> clients_;
};

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

TEST_CASE("peer_manager keys proxied peers by destination", "[peer_manager][socks5]") {
    ::asio::io_context ctx;
    peer_manager manager(ctx.get_executor());
    auto const settings = make_test_settings_pm();
    auto make = [&](std::string const& host) {
        return std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
            infrastructure::config::endpoint(host, 8333));
    };
    auto first = make("First.Example.");
    auto second = make("second.example");
    auto duplicate = make("first.example");
    CHECK( ! first->has_numeric_authority());
    CHECK(first->destination().host() == "first.example");
    CHECK(first->authority_with_agent() == "first.example:8333");
    auto numeric = make("[2001:DB8::1]");
    CHECK(numeric->has_numeric_authority());
    CHECK(numeric->authority().to_hostname() == "[2001:db8::1]");

    std::vector<code> admissions;
    std::vector<bool> found;
    bool done = false;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        admissions.push_back(co_await manager.add(first));
        admissions.push_back(co_await manager.add(second));
        admissions.push_back(co_await manager.add(duplicate));
        found.push_back(co_await manager.exists_by_destination(first->destination()));
        found.push_back(co_await manager.exists_by_destination(
            infrastructure::config::endpoint("missing.example", 8333)));
        found.push_back(co_await manager.exists_by_authority(infrastructure::config::authority::any()));
        found.push_back(co_await manager.exists_by_ip(::asio::ip::address_v6::any()));
        co_await manager.remove(first);
        found.push_back(co_await manager.exists_by_destination(first->destination()));
        manager.stop_all();
        found.push_back(co_await manager.exists_by_destination(second->destination()));
        done = true;
    }, ::asio::detached);
    run_until_pm(ctx, [&] { return done; });
    REQUIRE(done);
    REQUIRE(admissions.size() == 3);
    CHECK(admissions[0] == error::success);
    CHECK(admissions[1] == error::success);
    CHECK(admissions[2] == error::address_in_use);
    CHECK(found == std::vector<bool>{true, false, false, false, false, false});
    kth::test::drain_context(ctx);
    CHECK(manager.count_snapshot() == 0);
}

// =============================================================================
// Add/Remove Tests
// =============================================================================

TEST_CASE("peer_manager add peer", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = pool.make();
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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 5; ++i) {
        auto peer = pool.make();
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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer1 = pool.make();
    peer1->set_nonce(12345);

    auto peer2 = pool.make();
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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = pool.make();
    peer->set_nonce(12345);

    std::atomic<bool> done{false};

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        admissions.push_back(co_await manager.add(peer));
        REQUIRE(manager.count_snapshot() == 1);

        co_await manager.remove(peer);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 1);

    REQUIRE(manager.count_snapshot() == 0);
}

TEST_CASE("peer_manager remove by nonce", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = pool.make();
    peer->set_nonce(12345);

    std::atomic<bool> done{false};

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        admissions.push_back(co_await manager.add(peer));
        REQUIRE(manager.count_snapshot() == 1);

        co_await manager.remove_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 1);

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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 3);  // Max 3 peers

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 5; ++i) {
        auto peer = pool.make();
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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = pool.make();
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    bool exists_before = true;
    bool exists_after = false;

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        exists_before = co_await manager.exists_by_nonce(12345);
        admissions.push_back(co_await manager.add(peer));
        exists_after = co_await manager.exists_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 1);

    REQUIRE(!exists_before);
    REQUIRE(exists_after);
}

TEST_CASE("peer_manager find by nonce", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    auto peer = pool.make();
    peer->set_nonce(12345);

    std::atomic<bool> done{false};
    peer_session::ptr found_before = nullptr;
    peer_session::ptr found_after = nullptr;

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        found_before = co_await manager.find_by_nonce(12345);
        admissions.push_back(co_await manager.add(peer));
        found_after = co_await manager.find_by_nonce(12345);
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 1);

    REQUIRE(found_before == nullptr);
    REQUIRE(found_after != nullptr);
    REQUIRE(found_after.get() == peer.get());
}

TEST_CASE("peer_manager count", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::atomic<bool> done{false};
    size_t count1 = 999;
    size_t count2 = 999;

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        count1 = co_await manager.count();

        auto peer = pool.make();
        peer->set_nonce(12345);
        admissions.push_back(co_await manager.add(peer));

        count2 = co_await manager.count();
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 1);

    REQUIRE(count1 == 0);
    REQUIRE(count2 == 1);
}

TEST_CASE("peer_manager all", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> added_peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = pool.make();
        peer->set_nonce(1000 + i);
        added_peers.push_back(peer);
    }

    std::atomic<bool> done{false};
    std::vector<peer_session::ptr> retrieved_peers;

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : added_peers) {
            admissions.push_back(co_await manager.add(peer));
        }
        retrieved_peers = co_await manager.all();
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 3);

    REQUIRE(retrieved_peers.size() == 3);
}

// =============================================================================
// for_each Tests
// =============================================================================

TEST_CASE("peer_manager for_each", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = pool.make();
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<bool> done{false};
    std::atomic<int> visited_count{0};

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            admissions.push_back(co_await manager.add(peer));
        }

        co_await manager.for_each([&](peer_session::ptr) {
            ++visited_count;
        });

        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 3);

    REQUIRE(visited_count == 3);
}

TEST_CASE("peer_manager for_each skips stopped peers", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = pool.make();
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    // Stop one peer
    peers[1]->stop();

    std::atomic<bool> done{false};
    std::atomic<int> visited_count{0};

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            admissions.push_back(co_await manager.add(peer));
        }

        co_await manager.for_each([&](peer_session::ptr) {
            ++visited_count;
        });

        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 3);

    REQUIRE(visited_count == 2);  // Only 2 active peers visited
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("peer_manager stop_all", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    std::vector<peer_session::ptr> peers;
    for (int i = 0; i < 3; ++i) {
        auto peer = pool.make();
        peer->set_nonce(1000 + i);
        peers.push_back(peer);
    }

    std::atomic<bool> done{false};

    std::vector<code> admissions;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        for (auto const& peer : peers) {
            admissions.push_back(co_await manager.add(peer));
        }
        done = true;
    }, ::asio::detached);

    run_until_pm(ctx, [&]{ return done.load(); });
    require_admitted(admissions, 3);

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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    manager.stop_all();

    auto peer = pool.make();
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
    test_peers pool(ctx);
    peer_manager manager(ctx.get_executor(), 100);

    constexpr int num_operations = 50;
    std::atomic<int> completed{0};
    std::vector<std::optional<code>> admitted(num_operations);
    std::vector<std::string> authorities(num_operations);

    // Spawn many concurrent add operations
    for (int i = 0; i < num_operations; ++i) {
        ::asio::co_spawn(ctx, [&, i]() -> ::asio::awaitable<void> {
            auto peer = pool.make();
            peer->set_nonce(uint64_t(i));
            authorities[i] = peer->authority().to_string();
            admitted[i] = co_await manager.add(peer);

            // Immediately remove half
            if (i % 2 == 0) {
                co_await manager.remove(peer);
            }

            ++completed;
        }, ::asio::detached);
    }

    run_until_pm(ctx, [&]{ return completed.load() >= num_operations; }, 5000ms);

    REQUIRE(completed == num_operations);

    // Every session is a different peer — its own nonce, its own remote
    // endpoint — so every one of them is admitted. What remains is then down
    // to the removals alone.
    CHECK(std::set<std::string>(authorities.begin(), authorities.end()).size()
        == size_t(num_operations));
    for (int i = 0; i < num_operations; ++i) {
        INFO("peer " << i << " at " << authorities[i]);
        REQUIRE(admitted[i].has_value());
        CHECK(*admitted[i] == error::success);
    }

    // Half should remain (odd numbered)
    REQUIRE(manager.count_snapshot() == num_operations / 2);
}

// =============================================================================
// Destructor Test
// =============================================================================

TEST_CASE("peer_manager destructor stops peers", "[peer_manager]") {
    ::asio::io_context ctx;
    test_peers pool(ctx);
    std::vector<peer_session::ptr> peers;

    {
        peer_manager manager(ctx.get_executor(), 100);

        for (int i = 0; i < 3; ++i) {
            auto peer = pool.make();
            peer->set_nonce(1000 + i);
            peers.push_back(peer);
        }

        std::atomic<bool> done{false};

        std::vector<code> admissions;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            for (auto const& peer : peers) {
                admissions.push_back(co_await manager.add(peer));
            }
            done = true;
        }, ::asio::detached);

        run_until_pm(ctx, [&]{ return done.load(); });
        require_admitted(admissions, 3);

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
