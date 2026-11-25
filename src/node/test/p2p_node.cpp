// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <test_helpers.hpp>
#include <filesystem>

#include <kth/network.hpp>
#include <kth/node/p2p_node.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

using namespace kth;
using namespace kth::network;
using namespace kth::node;
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
    settings.hosts_file = std::filesystem::temp_directory_path() / ("kth_test_hosts_nonexistent.dat");
    settings.banlist_file = std::filesystem::temp_directory_path() / ("kth_test_banlist_nonexistent.dat");
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

TEST_CASE("p2p_node leaves proxy hostname peers out of the peer database", "[p2p_node][socks5]") {
    auto settings = make_test_settings_p2p();
    settings.peers_file.clear();
    settings.proxy = "127.0.0.1:9050";
    p2p_node node(settings);
    ::asio::io_context ctx;
    auto first = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
        infrastructure::config::endpoint("first.example", 8333));
    auto second = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
        infrastructure::config::endpoint("second.example", 8333));
    auto const unspecified = infrastructure::config::authority::any();
    REQUIRE( ! first->has_numeric_authority());
    REQUIRE( ! second->has_numeric_authority());
    REQUIRE(first->authority() == unspecified);
    REQUIRE(second->authority() == unspecified);
    // Reports under the ban threshold cannot add up without a record, so they
    // leave the peer connected; one at the threshold disconnects it.
    CHECK( ! node.report_misbehavior(first, 60, "timeout"));
    CHECK( ! node.report_misbehavior(first, 60, "timeout"));
    CHECK( ! first->stopped());
    CHECK( ! node.peer_db().get(unspecified));
    CHECK(node.report_misbehavior(second, peer_session::misbehavior_ban_threshold, "invalid block"));
    CHECK(second->stopped());
    CHECK( ! node.is_banned(unspecified));
    CHECK( ! node.peer_db().get(unspecified));
    for (int i = 0; i < 3; ++i) {
        node.record_peer_performance(first, 1, 1000);
        node.record_peer_performance(second, 1, 1000);
    }
    CHECK( ! node.is_slow_peer(unspecified));
    CHECK(node.get_peer_speed(unspecified) == 0.0);
    node.ban_peer(second, 60s);
    CHECK( ! node.is_banned(unspecified));
    CHECK( ! node.peer_db().get(unspecified));
    CHECK(node.peer_db().size() == 0);
    ctx.poll();
}

TEST_CASE("p2p_node bans a numeric proxy destination under its own address", "[p2p_node][socks5]") {
    auto settings = make_test_settings_p2p();
    settings.peers_file.clear();
    settings.proxy = "127.0.0.1:9050";
    p2p_node node(settings);
    ::asio::io_context ctx;
    auto peer = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
        infrastructure::config::endpoint("192.0.2.1", 8333));
    auto const address = infrastructure::config::authority::parse_from("192.0.2.1", 8333);
    REQUIRE(address);
    REQUIRE(peer->has_numeric_authority());
    REQUIRE(peer->authority() == *address);
    CHECK( ! node.report_misbehavior(peer, 40));
    CHECK( ! peer->stopped());
    CHECK(node.report_misbehavior(peer, 60, "invalid block"));
    CHECK(peer->stopped());
    CHECK(node.is_banned(*address));
    REQUIRE(node.peer_db().get(*address));
    CHECK(node.peer_db().get(*address)->reputation_score == 100);
    for (int i = 0; i < 3; ++i) {
        node.record_peer_performance(peer, 1, 1000);
    }
    CHECK(node.is_slow_peer(*address));
    CHECK(node.get_peer_speed(*address) == 1000.0);
    CHECK( ! node.is_banned(infrastructure::config::authority::any()));
    CHECK( ! node.peer_db().get(infrastructure::config::authority::any()));
    CHECK(node.peer_db().size() == 1);
    auto second = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
        infrastructure::config::endpoint("192.0.2.2", 8333));
    node.ban_peer(second, 60s);
    CHECK(node.is_banned(second->authority()));
    CHECK(second->stopped());
    ctx.poll();
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
            connect_result = co_await node.connect("invalid.host.that.does.not.exist.invalid", 18333);
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

#include <kth/node/detail/outbound_connections.hpp>

TEST_CASE("p2p_node - inbound peers do not count toward the outbound target", "[p2p_node]") {
    // Clients that connected to the node (browsers over WebSocket among them)
    // are not connections it opened: they leave its outbound target unmet.
    ::asio::io_context context;
    ::asio::ip::tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    network::settings settings(domain::config::network::regtest);
    std::vector<::asio::ip::tcp::socket> clients;
    std::vector<peer_session::ptr> peers;
    auto session = [&](bool inbound) {
        clients.emplace_back(context);
        clients.back().connect(acceptor.local_endpoint());
        peers.push_back(std::make_shared<peer_session>(acceptor.accept(), settings, inbound));
    };
    for (int i = 0; i < 5; ++i) session(true);
    session(false);
    session(false);

    auto counts = kth::node::detail::count_peers(peers);
    CHECK(counts.inbound == 5);
    CHECK(counts.outbound == 2);

    // A stopped peer is no longer connected either way.
    peers[5]->stop();
    peers[0]->stop();
    counts = kth::node::detail::count_peers(peers);
    CHECK(counts.inbound == 4);
    CHECK(counts.outbound == 1);
}

// =============================================================================
// A ping asked for out of schedule (the ping RPC)
// =============================================================================

#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/detail/p2p_node_test_seam.hpp>

#include "child_process_harness.hpp"

namespace {

void ping_checked(bool condition, char const* message) {
    if ( ! condition) throw std::runtime_error(message);
}

// The node's next peer notification (its channel is a selection channel).
::asio::awaitable<kth::node::peer_notification> next_peer_event(kth::node::p2p_node& target) {
    auto executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(target.peer_events()))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::use_awaitable));
    auto& [ec, selected] = completion;
    if (ec) throw std::runtime_error("peer notifications closed");
    co_return std::move(*selected.value<0>());
}

// The nonce of the next ping `client` receives by `limit` from now; nothing
// when none does.
::asio::awaitable<std::optional<uint64_t>> next_ping(peer_session& client,
    std::chrono::steady_clock::duration limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (true) {
        auto received = co_await client.receive(deadline);
        if ( ! received) co_return std::nullopt;
        if (received->heading.command() != domain::message::ping::command) continue;
        byte_reader reader(received->payload);
        auto const parsed = domain::message::ping::from_data(reader, client.negotiated_version());
        ping_checked(parsed.has_value(), "unreadable ping");
        co_return parsed->nonce();
    }
}

} // namespace

TEST_CASE("p2p child: a ping asked for out of schedule goes out on the peer's next turn", "[.p2p-child]") {
    using namespace kth::test;
    using namespace ::asio::experimental::awaitable_operators;
    watchdog guard(60s, "a ping asked for out of schedule");
    auto const directory = claimed_dir("kth_queued_ping");
    {
    network::settings config(domain::config::network::regtest);
    config.inbound_port = 0;
    config.outbound_connections = 0;
    config.host_pool_capacity = 64;
    config.seeds.clear();
    config.peers.clear();
    config.threads = 1;
    config.peers_file = (directory / "peers.dat").string();
    config.hosts_file = (directory / "hosts.dat").string();
    config.banlist_file = (directory / "bans.dat").string();
    config.channel_handshake_seconds = 5;
    // An hour: no ping on schedule can be one this test sees.
    config.channel_heartbeat_minutes = 60;
    config.user_agent = "/KTH-queued-ping-test:1/";
    config.user_agent_blacklist.clear();
    ::asio::io_context context;
    node::p2p_node target(config);
    std::exception_ptr failure;
    size_t asked = 0;
    std::optional<uint64_t> on_connect;
    std::optional<uint64_t> unasked;
    std::optional<uint64_t> requested;
    auto outcome = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        ping_checked(co_await target.start() == error::success, "node start failed");
        co_await (
            [&]() -> ::asio::awaitable<void> { co_await target.run(); }() &&
            [&]() -> ::asio::awaitable<void> {
                try {
                    auto executor = co_await ::asio::this_coro::executor;
                    ::asio::ip::tcp::acceptor listener(executor, {::asio::ip::address_v4::loopback(), 0});
                    ::asio::ip::tcp::socket client_socket(executor);
                    co_await client_socket.async_connect(listener.local_endpoint(), ::asio::use_awaitable);
                    auto accepted = co_await listener.async_accept(::asio::use_awaitable);
                    auto peer = std::make_shared<peer_session>(std::move(accepted), config, true);
                    auto client = std::make_shared<peer_session>(std::move(client_socket), config, false);
                    co_await kth::node::detail::p2p_node_test_seam::admit_peer(target, peer,
                        node::peer_direction::inbound);
                    auto handshake = co_await network::perform_handshake_direct(*client,
                        network::make_handshake_config(config, 0, 0x3000));
                    if ( ! handshake) throw std::runtime_error("fixture BCH handshake: " + handshake.error().message());
                    ping_checked((co_await next_peer_event(target)).event == node::peer_event_type::connected,
                        "missing connected notification");

                    // The test's end reads what the node sends from here on.
                    ::asio::co_spawn(executor, [client]() -> ::asio::awaitable<void> {
                        (void)co_await client->run();
                    }, ::asio::detached);

                    // The ping a peer gets on connecting (unless the handshake
                    // read it), then none on the hour's schedule...
                    on_connect = co_await next_ping(*client, 5s);
                    unasked = co_await next_ping(*client, 7s);
                    // ...until one is asked for: it goes out on the read loop's
                    // next turn, at most its five-second check interval away.
                    asked = co_await target.queue_pings();
                    requested = co_await next_ping(*client, 10s);
                    client->stop();
                } catch (...) { failure = std::current_exception(); }
                target.stop();
            }());
    }, ::asio::use_future);
    context.run();
    target.stop();
    target.join();
    outcome.get();
    if (failure) { std::rethrow_exception(failure); }
    CHECK(asked == 1u);
    CHECK_FALSE(unasked.has_value());
    REQUIRE(requested.has_value());
    // A ping of its own, with a nonce that is never zero (zero means none in flight).
    CHECK(*requested != 0u);
    if (on_connect) {
        CHECK(*requested != *on_connect);
    }
    } // Destroy the node and peer database before removing their files.
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    announce_completion();
}

TEST_CASE("p2p_node - a ping asked for out of schedule reaches the connected peer", "[p2p_node]") {
    kth::test::require_clean_child(kth::test::run_child(
        "p2p child: a ping asked for out of schedule goes out on the peer's next turn"));
}
