// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <test_helpers.hpp>
#include <filesystem>

#include <kth/network.hpp>
#include <kth/node/p2p_node.hpp>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

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

namespace {

// The node tests do not link src/network/test. This is the success path of the
// proxy there: method 0x00, then a CONNECT reply, then shutdown of the write
// side. connect() waits for a handshake after the duplicate and ban gates, and
// this proxy does not speak the peer protocol. The shutdown is after the
// counted CONNECT reply, so the SOCKS client still reads that reply and the
// handshake read is the one that sees end-of-file.
struct socks5_success_proxy {
    ::asio::io_context& ctx;
    ::asio::ip::tcp::acceptor listener;
    // accept or negotiate is blocked on this socket, and it is not in `held`
    // yet. close() runs only while serve() is suspended.
    std::shared_ptr<::asio::ip::tcp::socket> current;
    std::vector<::asio::ip::tcp::socket> held;
    std::atomic<int> negotiations{0};
    std::exception_ptr server_exception;
    std::atomic<bool> finished{false};

    explicit socks5_success_proxy(::asio::io_context& context)
        : ctx(context)
        , listener(context, {::asio::ip::address_v4::loopback(), 0})
    {}

    std::string proxy() const {
        return "127.0.0.1:" + std::to_string(listener.local_endpoint().port());
    }

    void start() {
        ::asio::co_spawn(ctx, serve(), [this](std::exception_ptr exception) {
            server_exception = exception;
            finished.store(true);
        });
    }

    void close() {
        std::error_code ignored;
        if (current) {
            current->close(ignored);
        }
        for (auto& socket : held) {
            socket.close(ignored);
        }
        listener.cancel(ignored);
    }

    ::asio::awaitable<bool> read_bytes(::asio::ip::tcp::socket& socket,
        std::vector<uint8_t>& bytes, size_t size) {
        auto const offset = bytes.size();
        bytes.resize(offset + size);
        auto [ec, count] = co_await ::asio::async_read(socket,
            ::asio::buffer(bytes.data() + offset, size), ::asio::as_tuple(::asio::use_awaitable));
        co_return ! ec && count == size;
    }

    ::asio::awaitable<bool> negotiate(::asio::ip::tcp::socket& socket) {
        std::vector<uint8_t> greeting;
        if ( ! co_await read_bytes(socket, greeting, 2)) {
            co_return false;
        }
        if ( ! co_await read_bytes(socket, greeting, greeting[1])) {
            co_return false;
        }
        std::vector<uint8_t> const method{0x05, 0x00};
        auto [method_ec, method_count] = co_await ::asio::async_write(socket,
            ::asio::buffer(method), ::asio::as_tuple(::asio::use_awaitable));
        if (method_ec || method_count != method.size()) {
            co_return false;
        }
        std::vector<uint8_t> request;
        if ( ! co_await read_bytes(socket, request, 4)) {
            co_return false;
        }
        size_t address_size = 0;
        switch (request[3]) {
            case 1:
                address_size = 4;
                break;
            case 4:
                address_size = 16;
                break;
            case 3:
                if ( ! co_await read_bytes(socket, request, 1)) {
                    co_return false;
                }
                address_size = request.back();
                break;
            default:
                co_return false;
        }
        if ( ! co_await read_bytes(socket, request, address_size + 2)) {
            co_return false;
        }
        std::vector<uint8_t> const reply{0x05, 0x00, 0x00, 0x01, 127, 0, 0, 1, 0, 0};
        auto [reply_ec, reply_count] = co_await ::asio::async_write(socket,
            ::asio::buffer(reply), ::asio::as_tuple(::asio::use_awaitable));
        if (reply_ec || reply_count != reply.size()) {
            co_return false;
        }
        std::error_code ignored;
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
        co_return true;
    }

    ::asio::awaitable<void> serve() {
        for (;;) {
            auto accepted = std::make_shared<::asio::ip::tcp::socket>(ctx);
            current = accepted;
            auto [ec] = co_await listener.async_accept(
                *accepted, ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;
            }
            if (co_await negotiate(*accepted)) {
                negotiations.fetch_add(1);
            }
            held.push_back(std::move(*accepted));
            current.reset();
        }
    }
};

// Outbound and inbound stay at 0, and seeds and manual peers are cleared, so
// the running node does not dial on its own. An empty peers file keeps a ban
// in memory: save() returns without opening peers.dat. Two seconds is the
// handshake budget for a connect that gets past the gates; the waits below
// are longer than that.
network::settings proxy_connect_settings(std::string const& proxy) {
    auto settings = make_test_settings_p2p();
    settings.outbound_connections = 0;
    settings.inbound_connections = 0;
    settings.inbound_port = 0;
    settings.seeds.clear();
    settings.peers.clear();
    settings.peers_file.clear();
    settings.channel_handshake_seconds = 2;
    settings.proxy = proxy;
    return settings;
}

void finish_proxy(socks5_success_proxy& proxy, ::asio::io_context& ctx) {
    proxy.close();
    run_until_p2p(ctx, [&]{ return proxy.finished.load(); }, 1000ms);
}

} // namespace

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

TEST_CASE("p2p_node connect rejects a banned numeric destination", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    std::atomic<bool> started{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    auto const banned = infrastructure::config::authority::parse_from("192.0.2.1", 8333);
    if (banned) {
        node.peer_db().ban(*banned, 60s);
    }

    std::atomic<bool> done{false};
    int negotiations = 0;
    std::expected<peer_session::ptr, code> connect_result;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        connect_result = co_await node.connect("192.0.2.1", 8333);
        negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return done.load(); }, 4000ms);

    node.stop();
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(banned.has_value());
    REQUIRE(node.is_banned(*banned));
    REQUIRE(done);
    // address_blocked follows a completed SOCKS negotiation, so the count is
    // sampled with the result.
    REQUIRE(negotiations >= 1);
    REQUIRE( ! connect_result.has_value());
    REQUIRE(connect_result.error() == error::address_blocked);
}

TEST_CASE("p2p_node connect rejects a numeric destination already in the manager", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    std::atomic<bool> started{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    std::atomic<bool> done{false};
    bool numeric = false;
    code add_result = error::unknown;
    size_t admitted = 0;
    int negotiations = 0;
    std::expected<peer_session::ptr, code> connect_result;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        auto session = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
            infrastructure::config::endpoint("192.0.2.2", 8333));
        numeric = session->has_numeric_authority();
        add_result = co_await node.peers().add(session);
        admitted = node.connection_count();
        connect_result = co_await node.connect("192.0.2.2", 8333);
        negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return done.load(); }, 4000ms);

    node.stop();
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(done);
    REQUIRE(numeric);
    REQUIRE(add_result == error::success);
    REQUIRE(admitted == 1);
    REQUIRE(negotiations >= 1);
    REQUIRE( ! connect_result.has_value());
    REQUIRE(connect_result.error() == error::address_in_use);
}

TEST_CASE("p2p_node connect rejects a duplicate hostname and still dials when the unspecified address is banned", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    // The gates return before the supervisor. A hostname that is not a
    // duplicate still waits there for a handshake result, so the supervisor
    // has to be running or that wait never ends. The proxy closes its write
    // side so the handshake read ends.
    std::atomic<bool> started{false};
    std::atomic<bool> run_returned{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
        if (start_result == error::success) {
            co_await node.run();
        }
        run_returned = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    std::atomic<bool> done{false};
    size_t count_before = 99;
    int first_negotiations = 0;
    bool hostname = false;
    code add_result = error::unknown;
    size_t admitted = 0;
    int second_negotiations = 0;
    bool unspecified_banned = false;
    int third_negotiations = 0;
    std::expected<peer_session::ptr, code> first;
    std::expected<peer_session::ptr, code> second;
    std::expected<peer_session::ptr, code> third;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        constexpr auto host = "duplicate-peer.invalid";
        count_before = node.connection_count();
        first = co_await node.connect(host, 8333);
        first_negotiations = proxy.negotiations.load();

        // connect() returns the session only after a handshake, which this
        // proxy cannot complete. The duplicate gate compares canonical
        // destinations, so admit a session for the name just requested.
        auto session = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
            infrastructure::config::endpoint(host, 8333));
        hostname = ! session->has_numeric_authority()
            && session->destination().host() == std::string(host);
        add_result = co_await node.peers().add(session);
        admitted = node.connection_count();
        second = co_await node.connect(host, 8333);
        second_negotiations = proxy.negotiations.load();

        node.peer_db().ban(infrastructure::config::authority::any(), 60s);
        unspecified_banned = node.is_banned(infrastructure::config::authority::any());
        third = co_await node.connect("other-peer.invalid", 8333);
        third_negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    // Two handshakes, each bounded by channel_handshake_seconds above.
    run_until_p2p(ctx, [&]{ return done.load(); }, 8000ms);

    node.stop();
    // stop() leaves the outbound idle timer armed. run() returns when that
    // timer ends. Pump until then, so the destructor's join finds the pool idle.
    run_until_p2p(ctx, [&]{ return run_returned.load(); }, 8000ms);
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(run_returned);
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(done);

    REQUIRE(count_before == 0);
    REQUIRE(first_negotiations >= 1);
    REQUIRE( ! first.has_value());
    REQUIRE(first.error() != error::address_in_use);

    REQUIRE(hostname);
    REQUIRE(add_result == error::success);
    REQUIRE(admitted == 1);
    REQUIRE(second_negotiations >= 2);
    REQUIRE( ! second.has_value());
    REQUIRE(second.error() == error::address_in_use);

    REQUIRE(unspecified_banned);
    REQUIRE(third_negotiations >= 3);
    REQUIRE( ! third.has_value());
    REQUIRE(third.error() != error::address_blocked);
    REQUIRE(third.error() != error::address_in_use);
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
