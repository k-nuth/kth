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

    auto version_payload = remote_version.to_data(70015);
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

    auto version_payload = remote_version.to_data(31402);
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
    auto ping_payload = ping.to_data(70015);
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
