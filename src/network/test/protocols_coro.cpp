// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <exception>
#include <future>
#include <optional>
#include <thread>

#include <test_helpers.hpp>

#include <kth/network.hpp>

#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

// =============================================================================
// Test helpers
// =============================================================================

std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>
make_connected_sockets_proto(::asio::io_context& ctx) {
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

template<typename Predicate>
void run_until_proto(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

network::settings make_test_settings_proto() {
    network::settings settings;
    settings.identifier = 0xe8f3e1e3;  // BCH testnet magic
    settings.protocol_maximum = 70015;
    settings.protocol_minimum = 31402;
    settings.services = 0;
    settings.invalid_services = 0;
    settings.validate_checksum = true;
    settings.channel_inactivity_minutes = 1;
    settings.channel_expiration_minutes = 5;
    settings.channel_handshake_seconds = 30;
    settings.inbound_port = 18333;
    settings.user_agent = "/KnuthTest:0.1.0/";
    return settings;
}

// Build a valid Bitcoin protocol message
data_chunk build_message_proto(std::string const& command, data_chunk const& payload, uint32_t magic) {
    data_chunk message;
    message.reserve(24 + payload.size());

    auto const magic_le = to_little_endian(magic);
    message.insert(message.end(), magic_le.begin(), magic_le.end());

    std::array<uint8_t, 12> cmd{};
    std::copy_n(command.begin(), std::min(command.size(), size_t{12}), cmd.begin());
    message.insert(message.end(), cmd.begin(), cmd.end());

    auto const size_le = to_little_endian(uint32_t(payload.size()));
    message.insert(message.end(), size_le.begin(), size_le.end());

    auto const hash = bitcoin_hash(payload);
    message.insert(message.end(), hash.begin(), hash.begin() + 4);

    message.insert(message.end(), payload.begin(), payload.end());

    return message;
}

// =============================================================================
// handshake_config Tests
// =============================================================================

TEST_CASE("make_handshake_config from settings", "[protocols_coro]") {
    auto settings = make_test_settings_proto();

    auto config = make_handshake_config(settings, 100000, 12345);

    REQUIRE(config.protocol_version == 70015);
    REQUIRE(config.minimum_version == 31402);
    REQUIRE(config.user_agent == "/KnuthTest:0.1.0/");
    REQUIRE(config.start_height == 100000);
    REQUIRE(config.nonce == 12345);
    REQUIRE(config.timeout == 30s);
}

// =============================================================================
// wait_for_any_message Tests
// =============================================================================

TEST_CASE("wait_for_any_message receives message", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> done{false};
    std::expected<raw_message, code> result = std::unexpected(error::unknown);

    // Start session
    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);

    // Wait for message
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await wait_for_any_message(*peer, 5s);
        done = true;
    }, ::asio::detached);

    ctx.run_for(50ms);

    // Send a ping message from client
    auto ping_msg = build_message_proto("ping", {}, settings.identifier);
    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(ping_msg), write_ec);
    REQUIRE(!write_ec);

    run_until_proto(ctx, [&]{ return done.load(); });

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(result.has_value());
    REQUIRE(result->heading.command() == "ping");
}

TEST_CASE("wait_for_any_message timeout", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> done{false};
    std::expected<raw_message, code> result = std::unexpected(error::unknown);

    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await wait_for_any_message(*peer, 1s);  // Short timeout
        done = true;
    }, ::asio::detached);

    // Don't send anything - should timeout
    run_until_proto(ctx, [&]{ return done.load(); }, 2000ms);

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == error::channel_timeout);
}

// =============================================================================
// Handshake Tests (simulated peer)
// =============================================================================

TEST_CASE("perform_handshake success", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);
    auto config = make_handshake_config(settings, 100000, 12345);

    std::atomic<bool> done{false};
    std::expected<handshake_result, code> result = std::unexpected(error::unknown);

    // Start peer session
    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);

    // Run handshake
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await perform_handshake(*peer, config);
        done = true;
    }, ::asio::detached);

    ctx.run_for(50ms);

    // Simulate remote peer: receive version, send version + verack

    // Read the version message our peer sent
    std::array<uint8_t, 1024> recv_buf;
    std::error_code read_ec;
    auto n = client.read_some(::asio::buffer(recv_buf), read_ec);
    REQUIRE(!read_ec);
    REQUIRE(n > 24);  // At least header size

    // Create and send remote version
    domain::message::version remote_version;
    remote_version.set_value(70015);
    remote_version.set_services(0);
    remote_version.set_timestamp(uint64_t(zulu_time()));
    remote_version.set_nonce(99999);
    remote_version.set_user_agent("/RemotePeer:1.0/");
    remote_version.set_start_height(50000);

    auto version_payload = kth::to_data_chunk(remote_version, 70015);
    auto version_msg = build_message_proto("version", version_payload, settings.identifier);
    ::asio::write(client, ::asio::buffer(version_msg), read_ec);
    REQUIRE(!read_ec);

    ctx.run_for(50ms);

    // Send verack
    auto verack_msg = build_message_proto("verack", {}, settings.identifier);
    ::asio::write(client, ::asio::buffer(verack_msg), read_ec);
    REQUIRE(!read_ec);

    run_until_proto(ctx, [&]{ return done.load(); }, 2000ms);

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(result.has_value());
    REQUIRE(result->peer_version != nullptr);
    REQUIRE(result->peer_version->user_agent() == "/RemotePeer:1.0/");
    REQUIRE(result->negotiated_version == 70015);
}

TEST_CASE("perform_handshake timeout", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);
    auto config = make_handshake_config(settings, 100000, 12345);
    config.timeout = 1s;  // Short timeout for test

    std::atomic<bool> done{false};
    std::expected<handshake_result, code> result = std::unexpected(error::unknown);

    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await perform_handshake(*peer, config);
        done = true;
    }, ::asio::detached);

    // Don't respond - should timeout
    run_until_proto(ctx, [&]{ return done.load(); }, 3000ms);

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == error::channel_timeout);
}

TEST_CASE("perform_handshake rejects low version peer", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);
    auto config = make_handshake_config(settings, 100000, 12345);
    config.minimum_version = 70000;  // Require higher version

    std::atomic<bool> done{false};
    std::expected<handshake_result, code> result = std::unexpected(error::unknown);

    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await perform_handshake(*peer, config);
        done = true;
    }, ::asio::detached);

    ctx.run_for(50ms);

    // Read version from our peer
    std::array<uint8_t, 1024> recv_buf;
    std::error_code read_ec;
    client.read_some(::asio::buffer(recv_buf), read_ec);

    // Send version with low protocol version
    domain::message::version remote_version;
    remote_version.set_value(31402);  // Too low
    remote_version.set_services(0);
    remote_version.set_timestamp(uint64_t(zulu_time()));
    remote_version.set_nonce(99999);
    remote_version.set_user_agent("/OldPeer:0.1/");
    remote_version.set_start_height(50000);

    auto version_payload = kth::to_data_chunk(remote_version, 31402);
    auto version_msg = build_message_proto("version", version_payload, settings.identifier);
    ::asio::write(client, ::asio::buffer(version_msg), read_ec);

    run_until_proto(ctx, [&]{ return done.load(); }, 2000ms);

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == error::channel_stopped);
}

// =============================================================================
// Ping/Pong Tests
// =============================================================================

TEST_CASE("run_ping_pong responds to ping", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);

    // Start peer session and ping/pong loop
    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);
    ::asio::co_spawn(ctx, run_ping_pong(*peer, 60s), ::asio::detached);  // Long interval so we don't auto-ping

    ctx.run_for(50ms);

    // Send a ping from client
    domain::message::ping ping{123456789};
    auto ping_payload = kth::to_data_chunk(ping, 70015);
    auto ping_msg = build_message_proto("ping", ping_payload, settings.identifier);

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(ping_msg), write_ec);
    REQUIRE(!write_ec);

    // Should receive pong in response
    std::array<uint8_t, 1024> recv_buf;
    std::atomic<size_t> bytes_received{0};
    std::string received_command;

    client.async_read_some(::asio::buffer(recv_buf), [&](auto ec, size_t n) {
        if (!ec && n >= 24) {
            bytes_received = n;
            // Extract command from header (bytes 4-16)
            std::string cmd(reinterpret_cast<char*>(recv_buf.data() + 4), 12);
            cmd = cmd.c_str();  // Trim nulls
            received_command = cmd;
        }
    });

    run_until_proto(ctx, [&]{ return bytes_received.load() > 0; });

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(bytes_received > 0);
    REQUIRE(received_command == "pong");
}

TEST_CASE("run_ping_pong sends periodic pings", "[protocols_coro]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings_proto();
    auto [client, server] = make_connected_sockets_proto(ctx);

    auto peer = std::make_shared<peer_session>(std::move(server), settings);

    // Short ping interval for testing
    ::asio::co_spawn(ctx, peer->run(), ::asio::detached);
    ::asio::co_spawn(ctx, run_ping_pong(*peer, 1s), ::asio::detached);

    // Wait for a ping to be sent
    std::array<uint8_t, 1024> recv_buf;
    std::atomic<bool> received_ping{false};

    std::function<void(std::error_code, size_t)> read_handler;
    read_handler = [&](std::error_code ec, size_t n) {
        if (!ec && n >= 24) {
            std::string cmd(reinterpret_cast<char*>(recv_buf.data() + 4), 12);
            cmd = cmd.c_str();
            if (cmd == "ping") {
                received_ping = true;
                return;
            }
        }
        if (!peer->stopped()) {
            client.async_read_some(::asio::buffer(recv_buf), read_handler);
        }
    };
    client.async_read_some(::asio::buffer(recv_buf), read_handler);

    run_until_proto(ctx, [&]{ return received_ping.load(); }, 3000ms);

    peer->stop();
    kth::test::drain_context(ctx);

    REQUIRE(received_ping);
}

namespace {

struct overlay_protocol_pair {
    ::asio::io_context ctx;
    ::asio::ip::tcp::acceptor listener{ctx, {::asio::ip::address_v4::loopback(), 0}};
    ::asio::ip::tcp::socket client{ctx};
    ::asio::ip::tcp::socket server{ctx};
    peer_session::ptr sender;
    peer_session::ptr receiver;
    bool accepted{false};
    bool connected{false};

    bool connect() {
        auto const port = listener.local_endpoint().port();
        listener.async_accept(server, [this](std::error_code ec) { accepted = ! ec; });
        client.async_connect({::asio::ip::address_v4::loopback(), port},
            [this](std::error_code ec) { connected = ! ec; });
        ctx.run_for(2s);
        ctx.restart();
        return accepted && connected;
    }

    void finish() {
        if (sender) sender->stop();
        if (receiver) receiver->stop();
        std::error_code ignored;
        listener.close(ignored);
        client.close(ignored);
        server.close(ignored);
        ctx.restart();
        ctx.run_for(100ms);
    }

    ~overlay_protocol_pair() { finish(); }
};

struct overlay_address_result {
    std::optional<std::expected<infrastructure::message::network_address::list, code>> addresses;
    std::string request_command;
    std::string command;
    code send_result{error::unknown};
    std::exception_ptr exception;
    bool done{false};
    bool routed{false};
};

::asio::awaitable<code> serve_overlay_address_request(peer_session::ptr sender,
    peer_session::ptr receiver, infrastructure::message::network_address::list addresses,
    std::shared_ptr<overlay_address_result> state)
{
    auto [request_ec, request] = co_await sender->messages().async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (request_ec) co_return error::channel_stopped;
    state->request_command = request.heading.command();
    if (state->request_command != "getaddr") co_return error::bad_stream;
    state->send_result = co_await send_addresses_auto(*sender, addresses);
    if (state->send_result != error::success) co_return state->send_result;
    auto [response_ec, response] = co_await receiver->messages().async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (response_ec) co_return error::channel_stopped;
    state->command = response.heading.command();
    auto executor = co_await ::asio::this_coro::executor;
    auto delivered = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(receiver->addr_responses(), std::move(response)),
        ::asio::experimental::after(std::chrono::steady_clock::now() + 1s)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = delivered;
    if (ec || selected.index != 0) co_return error::channel_timeout;
    co_return error::success;
}

awaitable_expected<handshake_result> handshake_overlay_peer(peer_session::ptr peer,
    handshake_config config)
{
    co_return co_await perform_handshake_direct(*peer, config);
}

} // namespace

TEST_CASE("send_addresses_auto retains overlays only after sendaddrv2", "[protocols_coro][addrv2][i2p]") {
    bool addrv2 = false;
    SECTION("negotiated addrv2") { addrv2 = true; }
    SECTION("legacy addr") {}
    overlay_protocol_pair pair;
    REQUIRE(pair.connect());
    auto settings = make_test_settings_proto();
    settings.protocol_maximum = domain::message::version::level::feature_negotiation;
    pair.sender = std::make_shared<peer_session>(std::move(pair.server), settings);
    pair.receiver = std::make_shared<peer_session>(std::move(pair.client), settings);
    pair.sender->set_wants_addrv2(addrv2);
    infrastructure::message::overlay_address bytes{};
    bytes.fill(0x42);
    infrastructure::message::network_address::list const addresses{
        {1234567890, 1, infrastructure::message::address_network::i2p, bytes, 0},
        {1234567891, 5, infrastructure::message::address_network::torv3, bytes, 8333},
        {1234567892, 9, infrastructure::config::authority::parse_from("8.8.8.8", 8333)->ip(), 8333}
    };
    auto state = std::make_shared<overlay_address_result>();
    ::asio::co_spawn(pair.ctx, pair.sender->run(), [state](std::exception_ptr ep, code) {
        if (ep) state->exception = ep;
    });
    ::asio::co_spawn(pair.ctx, pair.receiver->run(), [state](std::exception_ptr ep, code) {
        if (ep) state->exception = ep;
    });
    ::asio::co_spawn(pair.ctx, serve_overlay_address_request(pair.sender, pair.receiver, addresses, state),
        [state](std::exception_ptr ep, code ec) {
            if (ep) state->exception = ep;
            state->routed = ec == error::success;
        });
    ::asio::co_spawn(pair.ctx, request_addresses(*pair.receiver, 1s),
        [state](std::exception_ptr ep, std::expected<infrastructure::message::network_address::list, code> result) {
            if (ep) state->exception = ep;
            state->addresses.emplace(std::move(result));
            state->done = true;
        });
    run_until_proto(pair.ctx, [&] { return state->done && state->routed; }, 2000ms);
    pair.finish();
    REQUIRE(state->done);
    REQUIRE(state->routed);
    REQUIRE( ! state->exception);
    REQUIRE(state->send_result == error::success);
    REQUIRE(state->addresses);
    REQUIRE(*state->addresses);
    auto const& received = **state->addresses;
    CHECK(state->request_command == "getaddr");
    CHECK(state->command == (addrv2 ? "addrv2" : "addr"));
    if (addrv2) {
        CHECK(received == addresses);
        REQUIRE(received.size() == addresses.size());
        for (size_t i = 0; i < received.size(); ++i) {
            CHECK(received[i].timestamp() == addresses[i].timestamp());
        }
        peer_database db;
        for (auto const& address : received) CHECK(db.store_address(address));
        CHECK(db.size() == 3);
    } else {
        REQUIRE(received.size() == 1);
        CHECK(received.front() == addresses.back());
        CHECK( ! received.front().is_overlay());
    }
}

TEST_CASE("overlay stream completes a version handshake with legacy version addresses", "[protocols_coro][i2p]") {
    overlay_protocol_pair pair;
    REQUIRE(pair.connect());
    auto settings = make_test_settings_proto();
    settings.protocol_maximum = domain::message::version::level::feature_negotiation;
    infrastructure::message::overlay_address bytes{};
    auto const remote = infrastructure::config::authority(infrastructure::message::network_address(
        0, 1, infrastructure::message::address_network::i2p, bytes, 0));
    pair.sender = std::make_shared<peer_session>(std::move(pair.server), settings, false,
        infrastructure::config::endpoint(remote));
    pair.receiver = std::make_shared<peer_session>(std::move(pair.client), settings);
    struct handshake_completion {
        std::optional<std::expected<handshake_result, code>> first;
        std::optional<std::expected<handshake_result, code>> second;
        std::exception_ptr exception;
    };
    auto state = std::make_shared<handshake_completion>();
    auto first = make_handshake_config(settings, 1, 12345);
    auto second = make_handshake_config(settings, 2, 54321);
    first.timeout = 2s;
    second.timeout = 2s;
    ::asio::co_spawn(pair.ctx, handshake_overlay_peer(pair.sender, first),
        [state](std::exception_ptr ep, std::expected<handshake_result, code> result) {
            if (ep) state->exception = ep;
            state->first.emplace(std::move(result));
        });
    ::asio::co_spawn(pair.ctx, handshake_overlay_peer(pair.receiver, second),
        [state](std::exception_ptr ep, std::expected<handshake_result, code> result) {
            if (ep) state->exception = ep;
            state->second.emplace(std::move(result));
        });
    run_until_proto(pair.ctx, [&] { return state->first && state->second; }, 2000ms);
    pair.finish();
    REQUIRE( ! state->exception);
    REQUIRE(state->first);
    REQUIRE(state->second);
    REQUIRE(*state->first);
    REQUIRE(*state->second);
    CHECK(pair.sender->authority() == remote);
    CHECK(pair.sender->wants_addrv2());
    auto const& wire_version = (*state->second)->peer_version;
    REQUIRE(wire_version);
    CHECK( ! wire_version->address_receiver().is_overlay());
    CHECK(wire_version->address_receiver().ip() == infrastructure::message::null_address);
}
