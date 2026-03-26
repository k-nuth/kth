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

// Helper to run context with timeout and stop condition
template<typename Predicate>
void run_until_p2p(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

// Create default settings for testing
network::settings make_test_settings_p2p() {
    network::settings settings;
    settings.identifier = 0xe8f3e1e3;  // BCH testnet magic
    settings.protocol_maximum = 70015;
    settings.validate_checksum = true;
    settings.channel_inactivity_minutes = 1;
    settings.channel_expiration_minutes = 5;
    settings.inbound_port = 0;  // Disable inbound for unit tests
    settings.outbound_connections = 0;  // Disable outbound for unit tests
    settings.threads = 1;
    settings.connect_timeout_seconds = 5;
    settings.channel_handshake_seconds = 5;
    // Use non-existent paths for tests to avoid loading/saving real data
    settings.hosts_file = "/tmp/kth_test_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_test_banlist_nonexistent.dat";
    return settings;
}

// =============================================================================
// Construction Tests
// =============================================================================

TEST_CASE("p2p_node construction", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    REQUIRE(node.stopped());
    REQUIRE(node.connection_count() == 0);
}

TEST_CASE("p2p_node settings preserved", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    settings.identifier = 0x12345678;
    settings.threads = 4;

    p2p_node node(settings);

    REQUIRE(node.network_settings().identifier == 0x12345678);
    REQUIRE(node.network_settings().threads == 4);
}

// =============================================================================
// Top Block Tests
// =============================================================================

TEST_CASE("p2p_node top block default zero null hash", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    REQUIRE(node.top_block().height() == 0);
    REQUIRE(node.top_block().hash() == null_hash);
}

TEST_CASE("p2p_node set top block by const ref", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    size_t const expected_height = 42;
    auto const expected_hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    infrastructure::config::checkpoint const checkpoint{expected_hash, expected_height};
    node.set_top_block(checkpoint);

    REQUIRE(node.top_block().hash() == expected_hash);
    REQUIRE(node.top_block().height() == expected_height);
}

TEST_CASE("p2p_node set top block by rvalue", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    size_t const expected_height = 100;
    auto const expected_hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    node.set_top_block({expected_hash, expected_height});

    REQUIRE(node.top_block().hash() == expected_hash);
    REQUIRE(node.top_block().height() == expected_height);
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("p2p_node start stop", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    REQUIRE(node.stopped());

    // Start requires running in a coroutine context
    ::asio::io_context ctx;
    std::atomic<bool> started{false};
    code start_result = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
    }, ::asio::detached);

    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(!node.stopped());

    node.stop();
    REQUIRE(node.stopped());
}

TEST_CASE("p2p_node double start fails", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    ::asio::io_context ctx;
    std::atomic<int> done_count{0};
    code result1 = error::unknown;
    code result2 = error::unknown;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result1 = co_await node.start();
        ++done_count;
        result2 = co_await node.start();  // Second start should fail
        ++done_count;
    }, ::asio::detached);

    run_until_p2p(ctx, [&]{ return done_count.load() >= 2; }, 2000ms);

    REQUIRE(result1 == error::success);
    REQUIRE(result2 == error::operation_failed);

    node.stop();
}

TEST_CASE("p2p_node stop is idempotent", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    ::asio::io_context ctx;
    std::atomic<bool> started{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await node.start();
        started = true;
    }, ::asio::detached);

    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    node.stop();
    REQUIRE(node.stopped());

    node.stop();  // Second stop should be safe
    REQUIRE(node.stopped());
}

// =============================================================================
// Connection Count Tests
// =============================================================================

TEST_CASE("p2p_node connection count starts at zero", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    REQUIRE(node.connection_count() == 0);
}

// =============================================================================
// Address Management Tests
// =============================================================================

TEST_CASE("p2p_node address count starts at zero", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    REQUIRE(node.address_count() == 0);
}

// =============================================================================
// Peer Manager Access Tests
// =============================================================================

TEST_CASE("p2p_node peers access", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    // Should be able to access peer manager
    auto& manager = node.peers();
    REQUIRE(manager.count_snapshot() == 0);
}

// =============================================================================
// Connect Tests (requires network, marked as integration)
// =============================================================================

TEST_CASE("p2p_node connect to invalid host fails", "[p2p_node][integration]") {
    auto settings = make_test_settings_p2p();
    settings.connect_timeout_seconds = 1;  // Short timeout
    p2p_node node(settings);

    ::asio::io_context ctx;
    std::atomic<bool> done{false};
    code start_result = error::unknown;
    std::expected<peer_session::ptr, code> connect_result;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        if (start_result == error::success) {
            connect_result = co_await node.connect("invalid.host.that.does.not.exist.local", 18333);
        }
        done = true;
    }, ::asio::detached);

    run_until_p2p(ctx, [&]{ return done.load(); }, 5000ms);

    REQUIRE(done);
    REQUIRE(start_result == error::success);
    REQUIRE(!connect_result.has_value());

    node.stop();
}

TEST_CASE("p2p_node connect when stopped fails", "[p2p_node]") {
    auto settings = make_test_settings_p2p();
    p2p_node node(settings);

    ::asio::io_context ctx;
    std::atomic<bool> done{false};
    std::expected<peer_session::ptr, code> connect_result;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        // Don't start the node, try to connect
        connect_result = co_await node.connect("localhost", 18333);
        done = true;
    }, ::asio::detached);

    run_until_p2p(ctx, [&]{ return done.load(); }, 1000ms);

    REQUIRE(done);
    REQUIRE(!connect_result.has_value());
    REQUIRE(connect_result.error() == error::service_stopped);
}

// =============================================================================
// Destructor Tests
// =============================================================================

TEST_CASE("p2p_node destructor stops node", "[p2p_node]") {
    auto settings = make_test_settings_p2p();

    {
        p2p_node node(settings);

        ::asio::io_context ctx;
        std::atomic<bool> started{false};

        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            co_await node.start();
            started = true;
        }, ::asio::detached);

        run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

        REQUIRE(!node.stopped());
        // destructor called here
    }

    // Node should have been stopped by destructor
    // (no crash = success)
    REQUIRE(true);
}
