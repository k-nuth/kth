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
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

// =============================================================================
// Test helpers
// =============================================================================

// Create a connected socket pair for testing (client, server)
// Uses a background thread to handle the accept
std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>
make_connected_sockets(::asio::io_context& ctx) {
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = acceptor.local_endpoint().port();

    ::asio::ip::tcp::socket client(ctx);
    ::asio::ip::tcp::socket server(ctx);

    // Run accept in a thread
    std::thread accept_thread([&]() {
        server = acceptor.accept();
    });

    // Connect from main thread
    client.connect({::asio::ip::address_v4::loopback(), port});

    accept_thread.join();

    return {std::move(client), std::move(server)};
}

// Helper to run context with timeout and stop condition
template<typename Predicate>
void run_until(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

// Create default settings for testing
network::settings make_test_settings() {
    network::settings settings;
    settings.identifier = 0xe8f3e1e3;  // BCH testnet magic
    settings.protocol_maximum = 70015;
    settings.validate_checksum = true;
    settings.channel_inactivity_minutes = 1;
    settings.channel_expiration_minutes = 5;
    settings.inbound_port = 18333;
    return settings;
}

// Build a valid Bitcoin protocol message
data_chunk build_message(std::string const& command, data_chunk const& payload, uint32_t magic) {
    data_chunk message;
    message.reserve(24 + payload.size());

    // Magic (4 bytes, little-endian)
    auto const magic_le = to_little_endian(magic);
    message.insert(message.end(), magic_le.begin(), magic_le.end());

    // Command (12 bytes, null-padded)
    std::array<uint8_t, 12> cmd{};
    std::copy_n(command.begin(), std::min(command.size(), size_t{12}), cmd.begin());
    message.insert(message.end(), cmd.begin(), cmd.end());

    // Payload size (4 bytes, little-endian)
    auto const size_le = to_little_endian(uint32_t(payload.size()));
    message.insert(message.end(), size_le.begin(), size_le.end());

    // Checksum (4 bytes) - first 4 bytes of double SHA256
    auto const hash = bitcoin_hash(payload);
    message.insert(message.end(), hash.begin(), hash.begin() + 4);

    // Payload
    message.insert(message.end(), payload.begin(), payload.end());

    return message;
}

// =============================================================================
// Construction and Properties Tests
// =============================================================================

TEST_CASE("peer_session construction", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(!session->stopped());
    REQUIRE(session->negotiated_version() == settings.protocol_maximum);
}

TEST_CASE("peer_session authority is populated", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    auto const& auth = session->authority();
    REQUIRE(auth.port() != 0);
}

TEST_CASE("peer_session negotiated version get set", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(session->negotiated_version() == 70015);

    session->set_negotiated_version(70001);
    REQUIRE(session->negotiated_version() == 70001);

    session->set_negotiated_version(31402);
    REQUIRE(session->negotiated_version() == 31402);
}

TEST_CASE("peer_session nonce get set", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(session->nonce() == 0);

    session->set_nonce(0x123456789ABCDEF0);
    REQUIRE(session->nonce() == 0x123456789ABCDEF0);

    session->set_nonce(42);
    REQUIRE(session->nonce() == 42);
}

TEST_CASE("peer_session notify get set", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(session->notify() == true);

    session->set_notify(false);
    REQUIRE(session->notify() == false);

    session->set_notify(true);
    REQUIRE(session->notify() == true);
}

TEST_CASE("peer_session peer version get set", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(session->peer_version() == nullptr);

    auto version = std::make_shared<domain::message::version>();
    version->set_value(70015);
    version->set_services(1);
    version->set_user_agent("/Knuth:0.1.0/");

    session->set_peer_version(version);

    auto retrieved = session->peer_version();
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->value() == 70015);
    REQUIRE(retrieved->user_agent() == "/Knuth:0.1.0/");
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("peer_session stop", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    REQUIRE(!session->stopped());
    session->stop();
    REQUIRE(session->stopped());
}

TEST_CASE("peer_session stop is idempotent", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    session->stop();
    REQUIRE(session->stopped());

    // Multiple stops should not crash
    session->stop();
    session->stop(error::channel_timeout);
    REQUIRE(session->stopped());
}

TEST_CASE("peer_session stop with error code", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    session->stop(error::channel_timeout);
    REQUIRE(session->stopped());
}

TEST_CASE("peer_session run returns when stopped", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);
    std::atomic<bool> run_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Run a bit
    ctx.run_for(20ms);
    REQUIRE(!run_completed);

    // Stop and let it complete
    session->stop();
    run_until(ctx, [&]{ return run_completed.load(); });

    REQUIRE(run_completed);
}

// =============================================================================
// Message Send Tests
// =============================================================================

TEST_CASE("peer_session send raw data", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    // Prepare to receive on client
    std::array<uint8_t, 1024> recv_buffer;
    std::atomic<size_t> bytes_received{0};

    client.async_read_some(::asio::buffer(recv_buffer),
        [&](auto ec, size_t n) {
            if (!ec) bytes_received = n;
        });

    // Start session and send data
    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    data_chunk pong_payload;
    auto pong_msg = build_message("pong", pong_payload, settings.identifier);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->send_raw(pong_msg);
    }, ::asio::detached);

    run_until(ctx, [&]{ return bytes_received.load() > 0; });

    session->stop();
    kth::test::drain_context(ctx);

    REQUIRE(bytes_received == pong_msg.size());
}

TEST_CASE("peer_session send typed message ping", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    // Prepare to receive on client
    std::array<uint8_t, 1024> recv_buffer;
    std::atomic<size_t> bytes_received{0};

    client.async_read_some(::asio::buffer(recv_buffer),
        [&](auto ec, size_t n) {
            if (!ec) bytes_received = n;
        });

    // Start session
    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    // Send typed ping message
    domain::message::ping ping_msg(12345);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->send(ping_msg);
    }, ::asio::detached);

    run_until(ctx, [&]{ return bytes_received.load() > 0; });

    session->stop();
    kth::test::drain_context(ctx);

    // Should have received header (24 bytes) + payload (8 bytes for nonce)
    REQUIRE(bytes_received == 32);
}

TEST_CASE("peer_session send returns error when stopped", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);
    session->stop();

    std::atomic<bool> done{false};
    code send_result = error::success;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        send_result = co_await session->send(domain::message::ping(0));
        done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return done.load(); });

    REQUIRE(send_result == error::channel_stopped);
}

// =============================================================================
// Message Receive Tests
// =============================================================================

TEST_CASE("peer_session receive message", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> message_received{false};
    std::string received_command;

    // Start the session and receiver
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        auto [ec, msg] = co_await session->messages().async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (!ec) {
            message_received = true;
            received_command = msg.heading.command();
        }
    }, ::asio::detached);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    // Give session time to start
    ctx.run_for(10ms);

    // Send a ping message from client
    data_chunk ping_payload;
    auto ping_msg = build_message("ping", ping_payload, settings.identifier);

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(ping_msg), write_ec);
    REQUIRE(!write_ec);

    run_until(ctx, [&]{ return message_received.load(); });

    session->stop();
    kth::test::drain_context(ctx);

    REQUIRE(message_received);
    REQUIRE(received_command == "ping");
}

TEST_CASE("peer_session receive multiple messages", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<int> messages_received{0};

    // Start receiver
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!session->stopped() && messages_received < 5) {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                ++messages_received;
            } else {
                break;
            }
        }
    }, ::asio::detached);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    // Give session time to start
    ctx.run_for(10ms);

    // Send multiple messages
    for (int i = 0; i < 5; ++i) {
        auto msg = build_message("ping", {}, settings.identifier);
        std::error_code write_ec;
        ::asio::write(client, ::asio::buffer(msg), write_ec);
        REQUIRE(!write_ec);
    }

    run_until(ctx, [&]{ return messages_received.load() >= 5; });

    session->stop();
    kth::test::drain_context(ctx);

    REQUIRE(messages_received == 5);
}

TEST_CASE("peer_session receive message with payload", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> message_received{false};
    data_chunk received_payload;

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        auto [ec, msg] = co_await session->messages().async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (!ec) {
            message_received = true;
            received_payload = std::move(msg.payload);
        }
    }, ::asio::detached);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    ctx.run_for(10ms);

    // Send ping with nonce payload (8 bytes)
    data_chunk ping_payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto msg = build_message("ping", ping_payload, settings.identifier);

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(msg), write_ec);
    REQUIRE(!write_ec);

    run_until(ctx, [&]{ return message_received.load(); });

    session->stop();
    kth::test::drain_context(ctx);

    REQUIRE(message_received);
    REQUIRE(received_payload.size() == 8);
    REQUIRE(received_payload == ping_payload);
}

// =============================================================================
// Protocol Validation Tests
// =============================================================================

TEST_CASE("peer_session invalid magic rejected", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    ctx.run_for(10ms);

    // Send message with wrong magic
    uint32_t wrong_magic = 0xdeadbeef;
    data_chunk bad_msg = build_message("ping", {}, wrong_magic);

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(bad_msg), write_ec);
    REQUIRE(!write_ec);

    run_until(ctx, [&]{ return session->stopped(); });

    REQUIRE(session->stopped());
}

TEST_CASE("peer_session invalid checksum rejected", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    ctx.run_for(10ms);

    // Build message then corrupt checksum
    auto msg = build_message("ping", {}, settings.identifier);
    msg[20] ^= 0xFF;

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(msg), write_ec);
    REQUIRE(!write_ec);

    run_until(ctx, [&]{ return session->stopped(); });

    REQUIRE(session->stopped());
}

TEST_CASE("peer_session oversized payload rejected", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    ctx.run_for(10ms);

    // Build header claiming huge payload
    data_chunk bad_header;

    auto magic_le = to_little_endian(settings.identifier);
    bad_header.insert(bad_header.end(), magic_le.begin(), magic_le.end());

    std::array<uint8_t, 12> cmd{};
    std::copy_n("ping", 4, cmd.begin());
    bad_header.insert(bad_header.end(), cmd.begin(), cmd.end());

    // 100 MB - way too big
    auto size_le = to_little_endian(uint32_t{100 * 1024 * 1024});
    bad_header.insert(bad_header.end(), size_le.begin(), size_le.end());

    bad_header.insert(bad_header.end(), {0, 0, 0, 0});

    std::error_code write_ec;
    ::asio::write(client, ::asio::buffer(bad_header), write_ec);
    REQUIRE(!write_ec);

    run_until(ctx, [&]{ return session->stopped(); });

    REQUIRE(session->stopped());
}

// =============================================================================
// Connection Close Tests
// =============================================================================

TEST_CASE("peer_session connection closed by peer", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    ctx.run_for(50ms);

    // Close client connection
    client.close();

    run_until(ctx, [&]{ return session->stopped(); });

    REQUIRE(session->stopped());
}

TEST_CASE("peer_session graceful shutdown", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);
    std::atomic<bool> run_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    ctx.run_for(50ms);

    session->stop(error::service_stopped);

    run_until(ctx, [&]{ return run_completed.load(); });

    REQUIRE(session->stopped());
    REQUIRE(run_completed);
}

// =============================================================================
// Concurrent Access Tests (thread safety)
// =============================================================================

TEST_CASE("peer_session concurrent property access", "[peer_session][concurrent]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    // Multiple threads accessing properties concurrently
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 100; ++j) {
                session->set_negotiated_version(70000 + i);
                [[maybe_unused]] auto v = session->negotiated_version();

                session->set_nonce(i * 1000 + j);
                [[maybe_unused]] auto n = session->nonce();

                session->set_notify(j % 2 == 0);
                [[maybe_unused]] auto notify = session->notify();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crashes = success
    REQUIRE(true);
}

// =============================================================================
// Timer Tests
// =============================================================================

TEST_CASE("peer_session inactivity timeout", "[peer_session][timer]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    // Very short inactivity timeout for testing (0 minutes = immediate)
    settings.channel_inactivity_minutes = 0;

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // With 0 minutes timeout, session should stop due to inactivity
    run_until(ctx, [&]{ return session->stopped(); }, 2000ms);

    REQUIRE(session->stopped());
}

// =============================================================================
// Bidirectional Communication Tests
// =============================================================================

TEST_CASE("peer_session bidirectional communication", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<int> messages_received{0};
    std::atomic<int> messages_sent{0};

    // Receiver coroutine - receives pings and sends pongs back
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!session->stopped() && messages_received < 3) {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                ++messages_received;
                // Echo back a pong for each ping
                co_await session->send(domain::message::pong(0));
                ++messages_sent;
            } else {
                break;
            }
        }
    }, ::asio::detached);

    ::asio::co_spawn(ctx, session->run(), ::asio::detached);

    // Give session time to start
    ctx.run_for(10ms);

    // Client receives pongs
    std::array<uint8_t, 1024> recv_buffer;
    std::atomic<int> pongs_received{0};

    std::function<void(std::error_code, size_t)> read_handler;
    read_handler = [&](std::error_code ec, size_t n) {
        if (!ec && n >= 24) {
            ++pongs_received;
            if (pongs_received < 3) {
                client.async_read_some(::asio::buffer(recv_buffer), read_handler);
            }
        }
    };
    client.async_read_some(::asio::buffer(recv_buffer), read_handler);

    // Send 3 pings from client
    for (int i = 0; i < 3; ++i) {
        auto ping_msg = build_message("ping", {}, settings.identifier);
        std::error_code write_ec;
        ::asio::write(client, ::asio::buffer(ping_msg), write_ec);
        REQUIRE(!write_ec);
    }

    run_until(ctx, [&]{ return messages_received.load() >= 3 && pongs_received.load() >= 1; }, 2000ms);

    session->stop();
    kth::test::drain_context(ctx);

    REQUIRE(messages_received == 3);
    REQUIRE(messages_sent == 3);
    REQUIRE(pongs_received >= 1);  // At least some pongs received
}

// =============================================================================
// Destructor Test
// =============================================================================

TEST_CASE("peer_session destructor stops session", "[peer_session]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    {
        auto session = std::make_shared<peer_session>(std::move(server), settings);
        ::asio::co_spawn(ctx, session->run(), ::asio::detached);
        ctx.run_for(50ms);

        // Stop session and drain context before destroying
        // This ensures all coroutines complete before the object is freed
        session->stop();
        kth::test::drain_context(ctx);
    }

    REQUIRE(true);
}

// =============================================================================
// async_listen / async_accept Tests
// =============================================================================

TEST_CASE("async_listen creates acceptor on port", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    std::atomic<bool> done{false};
    std::expected<::asio::ip::tcp::acceptor, code> result = std::unexpected(error::unknown);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await async_listen(ctx.get_executor(), 0);  // Port 0 = random available port
        done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return done.load(); });

    REQUIRE(result.has_value());
    REQUIRE(result->is_open());

    auto port = result->local_endpoint().port();
    REQUIRE(port != 0);

    result->close();
}

TEST_CASE("async_accept accepts connection", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    // Create acceptor directly (simpler than async_listen for this test)
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = acceptor.local_endpoint().port();
    REQUIRE(port != 0);

    std::atomic<bool> accept_done{false};
    std::expected<peer_session::ptr, code> accept_result = std::unexpected(error::unknown);

    // Connect from a separate thread (so it doesn't block io_context)
    std::thread client_thread([port]() {
        ::asio::io_context client_ctx;
        ::asio::ip::tcp::socket client(client_ctx);
        std::error_code ec;
        client.connect({::asio::ip::address_v4::loopback(), port}, ec);
        // Keep socket alive briefly
        std::this_thread::sleep_for(100ms);
    });

    // Accept in coroutine
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        accept_result = co_await async_accept(acceptor, settings);
        accept_done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return accept_done.load(); }, 2000ms);
    client_thread.join();

    REQUIRE(accept_result.has_value());
    REQUIRE(accept_result.value() != nullptr);
    REQUIRE(!accept_result.value()->stopped());

    accept_result.value()->stop();
    acceptor.close();
}

TEST_CASE("async_accept returns error when acceptor closed", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    std::atomic<bool> done{false};
    std::expected<peer_session::ptr, code> result = std::unexpected(error::unknown);

    // Create and close acceptor
    auto listen_result = ::asio::ip::tcp::acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = listen_result.local_endpoint().port();

    // Start accept then close
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await async_accept(listen_result, settings);
        done = true;
    }, ::asio::detached);

    ctx.run_for(10ms);
    listen_result.close();

    run_until(ctx, [&]{ return done.load(); });

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == error::service_stopped);
}

// =============================================================================
// async_connect Tests (these are [integration] since they need network)
// =============================================================================

TEST_CASE("async_connect to local acceptor", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    // Setup: create a listening socket
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = acceptor.local_endpoint().port();

    std::atomic<bool> connect_done{false};
    std::expected<peer_session::ptr, code> connect_result = std::unexpected(error::unknown);

    // Accept in background thread
    std::thread accept_thread([&]() {
        acceptor.accept();
    });

    // Connect
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        connect_result = co_await async_connect(
            ctx.get_executor(),
            "127.0.0.1",
            port,
            settings,
            std::chrono::seconds{5});
        connect_done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return connect_done.load(); }, 5000ms);
    accept_thread.join();

    REQUIRE(connect_result.has_value());
    REQUIRE(connect_result.value() != nullptr);
    REQUIRE(!connect_result.value()->stopped());

    connect_result.value()->stop();
    acceptor.close();
}

TEST_CASE("async_connect timeout", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    std::atomic<bool> done{false};
    std::expected<peer_session::ptr, code> result = std::unexpected(error::unknown);

    // Connect to a non-routable IP with short timeout
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await async_connect(
            ctx.get_executor(),
            "10.255.255.1",  // Non-routable, will timeout
            12345,
            settings,
            std::chrono::seconds{1});  // Short timeout
        done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return done.load(); }, 3000ms);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == error::channel_timeout);
}

TEST_CASE("async_connect with authority", "[peer_session][helpers]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    // Setup: create a listening socket
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto port = acceptor.local_endpoint().port();

    std::atomic<bool> done{false};
    std::expected<peer_session::ptr, code> result = std::unexpected(error::unknown);

    std::thread accept_thread([&]() {
        acceptor.accept();
    });

    // Create authority
    infrastructure::config::authority auth("127.0.0.1", port);

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        result = co_await async_connect(
            ctx.get_executor(),
            auth,
            settings,
            std::chrono::seconds{5});
        done = true;
    }, ::asio::detached);

    run_until(ctx, [&]{ return done.load(); }, 5000ms);
    accept_thread.join();

    REQUIRE(result.has_value());
    REQUIRE(result.value() != nullptr);

    result.value()->stop();
    acceptor.close();
}

// =============================================================================
// Concurrent Shutdown Tests (regression test for segfault during Ctrl-C)
// =============================================================================

// 2026-01-28: Test for segfault during concurrent shutdown of multiple peers.
// Symptom: When Ctrl-C is pressed during sync, multiple peer_sessions are stopped
// concurrently. The log shows crash mid-word during stop():
//   "[peer_session] Stopping session [31.220.93.253:8333]: channel stoppe"
// This test simulates that scenario by creating multiple sessions and stopping
// them all at once from different "threads" (coroutines).

TEST_CASE("peer_session concurrent shutdown of multiple sessions", "[peer_session][concurrent][shutdown]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    constexpr size_t num_peers = 10;

    // Create multiple socket pairs and sessions
    std::vector<std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>> socket_pairs;
    std::vector<peer_session::ptr> sessions;

    for (size_t i = 0; i < num_peers; ++i) {
        socket_pairs.push_back(make_connected_sockets(ctx));
        sessions.push_back(std::make_shared<peer_session>(
            std::move(socket_pairs.back().second), settings));
    }

    // Track how many sessions completed their run()
    std::atomic<size_t> run_completed{0};

    // Start all sessions running
    for (auto& session : sessions) {
        ::asio::co_spawn(ctx, [&, s = session]() -> ::asio::awaitable<void> {
            co_await s->run();
            run_completed.fetch_add(1, std::memory_order_relaxed);
        }, ::asio::detached);
    }

    // Let them run for a bit
    ctx.run_for(50ms);

    // Now stop ALL sessions concurrently from multiple coroutines
    // This simulates what happens during Ctrl-C: peer_manager stops all peers,
    // each peer's run() completes, and cleanup happens concurrently
    for (auto& session : sessions) {
        // Stop from multiple "sources" like in real shutdown:
        // 1. Direct stop call (like peer_manager.stop_all())
        session->stop(error::service_stopped);

        // 2. Another stop call (like from peer cleanup path)
        ::asio::co_spawn(ctx, [s = session]() -> ::asio::awaitable<void> {
            s->stop(error::channel_stopped);
            co_return;
        }, ::asio::detached);
    }

    // Wait for all sessions to complete
    run_until(ctx, [&]{ return run_completed.load() >= num_peers; }, 2000ms);

    // Drain any remaining work
    kth::test::drain_context(ctx);

    // Verify all sessions stopped
    for (auto const& session : sessions) {
        REQUIRE(session->stopped());
    }

    // If we get here without crashing, the test passes
    REQUIRE(run_completed == num_peers);
}

TEST_CASE("peer_session stop called from destructor during active run", "[peer_session][concurrent][shutdown]") {
    // This test verifies that destroying a peer_session while run() is active
    // doesn't cause a crash. The destructor calls stop() which should be safe.
    ::asio::io_context ctx;
    auto settings = make_test_settings();

    std::atomic<bool> run_started{false};
    std::atomic<bool> test_done{false};

    {
        auto [client, server] = make_connected_sockets(ctx);
        auto session = std::make_shared<peer_session>(std::move(server), settings);

        ::asio::co_spawn(ctx, [&, s = session]() -> ::asio::awaitable<void> {
            run_started = true;
            co_await s->run();
        }, ::asio::detached);

        // Wait for run to start
        run_until(ctx, [&]{ return run_started.load(); });

        // Stop the session explicitly before it goes out of scope
        session->stop();

        // The session shared_ptr is still held by the coroutine, so it won't
        // be destroyed yet. Let the context drain.
        kth::test::drain_context(ctx);
    }

    test_done = true;
    REQUIRE(test_done);
}

TEST_CASE("peer_session multiple stop calls from different threads", "[peer_session][concurrent][shutdown]") {
    ::asio::io_context ctx;
    auto settings = make_test_settings();
    auto [client, server] = make_connected_sockets(ctx);

    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    ctx.run_for(20ms);

    // Call stop from multiple threads simultaneously
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&, i]() {
            // Small random delay to increase race condition likelihood
            std::this_thread::sleep_for(std::chrono::microseconds(i * 10));
            session->stop(error::channel_stopped);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    run_until(ctx, [&]{ return run_completed.load(); });
    kth::test::drain_context(ctx);

    REQUIRE(session->stopped());
    REQUIRE(run_completed);
}

// =============================================================================
// ASAN Segfault Replication Test - parallel_group cancellation bug
// =============================================================================

// 2026-01-28: Test for ASAN segfault in asio::cancellation_signal::emit()
// when using the || operator with awaitable coroutines.
//
// Root cause from ASAN log (segfault-1.log):
//   #0 asio::cancellation_signal::emit(asio::cancellation_type)
//   #4 asio::experimental::detail::parallel_group_cancellation_handler<wait_for_one_success, ...>
//
// The || operator internally uses parallel_group with wait_for_one_success.
// When one coroutine completes, parallel_group tries to cancel the others via
// cancellation_signal::emit(). The bug manifests as a use-after-free when:
// 1. peer_session::run() is waiting on co_await (read_loop || write_loop || ...)
// 2. External stop() is called, which closes socket and channels
// 3. One loop completes due to the closure
// 4. parallel_group tries to cancel other loops but memory is already freed
//
// This test attempts to replicate the exact pattern by:
// - Creating peer_sessions with active run() coroutines
// - Rapidly stopping them while the || operator is active
// - Running many iterations to increase race condition probability

#include <asio/experimental/awaitable_operators.hpp>
#include <asio/thread_pool.hpp>

TEST_CASE("peer_session ASAN segfault replication - parallel_group cancellation",
          "[peer_session][concurrent][shutdown][asan]") {
    using namespace ::asio::experimental::awaitable_operators;

    // Run multiple iterations to increase chance of hitting the race
    constexpr int num_iterations = 100;
    constexpr size_t num_peers_per_iteration = 10;
    constexpr size_t num_threads = 4;

    for (int iter = 0; iter < num_iterations; ++iter) {
        // Use a single io_context with multiple threads running it.
        // This avoids cross-context executor issues that occur when mixing
        // io_context (for sockets) with thread_pool (for coroutines).
        ::asio::io_context ctx;

        auto settings = make_test_settings();
        settings.channel_inactivity_minutes = 1;
        settings.channel_expiration_minutes = 1;

        std::vector<std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>> socket_pairs;
        std::vector<peer_session::ptr> sessions;
        std::atomic<size_t> runs_started{0};
        std::atomic<size_t> runs_completed{0};

        // Create sessions
        for (size_t i = 0; i < num_peers_per_iteration; ++i) {
            socket_pairs.push_back(make_connected_sockets(ctx));
            sessions.push_back(std::make_shared<peer_session>(
                std::move(socket_pairs.back().second), settings));
        }

        // Start all sessions
        for (auto& session : sessions) {
            ::asio::co_spawn(ctx, [&, s = session]() -> ::asio::awaitable<void> {
                runs_started.fetch_add(1, std::memory_order_relaxed);
                co_await s->run();
                runs_completed.fetch_add(1, std::memory_order_relaxed);
            }, ::asio::detached);
        }

        // Run context on multiple threads for true parallelism
        std::vector<std::thread> ctx_threads;
        for (size_t i = 0; i < num_threads; ++i) {
            ctx_threads.emplace_back([&ctx]() {
                ctx.run();
            });
        }

        // Wait for all sessions to start
        while (runs_started.load() < num_peers_per_iteration) {
            std::this_thread::sleep_for(1ms);
        }

        // Brief delay to ensure they're deep in the || operator
        std::this_thread::sleep_for(5ms);

        // Now rapidly stop all sessions from MULTIPLE THREADS simultaneously
        std::vector<std::thread> stop_threads;
        for (size_t i = 0; i < num_peers_per_iteration; ++i) {
            stop_threads.emplace_back([&, idx = i]() {
                sessions[idx]->stop(error::service_stopped);
            });
        }

        // Also close client sockets from another set of threads
        for (size_t i = 0; i < num_peers_per_iteration; ++i) {
            stop_threads.emplace_back([&, idx = i]() {
                boost_code ignored;
                socket_pairs[idx].first.close(ignored);
            });
        }

        // Join all stop threads
        for (auto& t : stop_threads) {
            t.join();
        }

        // Wait for all runs to complete or timeout
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (runs_completed.load() < num_peers_per_iteration &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }

        // Stop the context and wait for threads
        ctx.stop();
        for (auto& t : ctx_threads) {
            t.join();
        }

        // Verify all sessions stopped
        for (auto const& session : sessions) {
            REQUIRE(session->stopped());
        }
    }

    REQUIRE(true);
}

// Additional stress test: rapid create/destroy cycles with active run()
TEST_CASE("peer_session rapid create destroy with active run",
          "[peer_session][concurrent][shutdown][asan]") {
    constexpr int num_iterations = 50;
    constexpr size_t num_threads = 4;

    for (int iter = 0; iter < num_iterations; ++iter) {
        ::asio::io_context ctx;
        auto settings = make_test_settings();

        std::atomic<bool> run_started{false};
        std::atomic<bool> run_completed{false};
        std::weak_ptr<peer_session> weak_session;

        auto [client, server] = make_connected_sockets(ctx);
        auto session = std::make_shared<peer_session>(std::move(server), settings);
        weak_session = session;

        ::asio::co_spawn(ctx, [&, s = session]() -> ::asio::awaitable<void> {
            run_started = true;
            co_await s->run();
            run_completed = true;
        }, ::asio::detached);

        // Run context on multiple threads
        std::vector<std::thread> ctx_threads;
        for (size_t i = 0; i < num_threads; ++i) {
            ctx_threads.emplace_back([&ctx]() { ctx.run(); });
        }

        // Wait for run() to start
        while (!run_started.load()) {
            std::this_thread::sleep_for(1ms);
        }
        std::this_thread::sleep_for(2ms);

        // Stop from a different thread while run() is active
        std::thread stop_thread([&]() {
            session->stop(error::service_stopped);
        });

        // Close the client socket from yet another thread
        std::thread close_thread([&]() {
            boost_code ignored;
            client.close(ignored);
        });

        stop_thread.join();
        close_thread.join();

        // Release our reference
        session.reset();

        // Wait for run to complete
        auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!run_completed.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }

        ctx.stop();
        for (auto& t : ctx_threads) {
            t.join();
        }

        REQUIRE(weak_session.expired());
    }
}

// Test with interleaved stop/cleanup pattern
TEST_CASE("peer_session interleaved stop cleanup pattern",
          "[peer_session][concurrent][shutdown][asan]") {
    constexpr size_t num_peers = 16;
    constexpr size_t num_threads = 4;
    constexpr int num_iterations = 50;

    for (int iter = 0; iter < num_iterations; ++iter) {
        ::asio::io_context ctx;
        auto settings = make_test_settings();

        std::vector<std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>> socket_pairs;
        std::vector<peer_session::ptr> sessions;
        std::atomic<size_t> runs_started{0};
        std::atomic<size_t> runs_completed{0};

        // Create all sessions
        for (size_t i = 0; i < num_peers; ++i) {
            socket_pairs.push_back(make_connected_sockets(ctx));
            sessions.push_back(std::make_shared<peer_session>(
                std::move(socket_pairs.back().second), settings));
        }

        // Start all sessions
        for (auto& session : sessions) {
            ::asio::co_spawn(ctx, [&, s = session]() -> ::asio::awaitable<void> {
                runs_started.fetch_add(1, std::memory_order_relaxed);
                co_await s->run();
                runs_completed.fetch_add(1, std::memory_order_relaxed);
            }, ::asio::detached);
        }

        // Run context on multiple threads
        std::vector<std::thread> ctx_threads;
        for (size_t i = 0; i < num_threads; ++i) {
            ctx_threads.emplace_back([&ctx]() { ctx.run(); });
        }

        // Wait for all to start
        while (runs_started.load() < num_peers) {
            std::this_thread::sleep_for(1ms);
        }
        std::this_thread::sleep_for(5ms);

        // Interleaved stop pattern from multiple threads
        std::vector<std::thread> stop_threads;

        // Stop odd-indexed sessions from separate threads
        for (size_t i = 1; i < num_peers; i += 2) {
            stop_threads.emplace_back([&, idx = i]() {
                sessions[idx]->stop(error::service_stopped);
            });
        }

        // Tiny delay
        std::this_thread::sleep_for(100us);

        // Stop even-indexed sessions from separate threads
        for (size_t i = 0; i < num_peers; i += 2) {
            stop_threads.emplace_back([&, idx = i]() {
                sessions[idx]->stop(error::service_stopped);
            });
        }

        // Close all client sockets from separate threads
        for (size_t i = 0; i < num_peers; ++i) {
            stop_threads.emplace_back([&, idx = i]() {
                boost_code ignored;
                socket_pairs[idx].first.close(ignored);
            });
        }

        // Join all threads
        for (auto& t : stop_threads) {
            t.join();
        }

        // Wait for completion
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (runs_completed.load() < num_peers &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }

        ctx.stop();
        for (auto& t : ctx_threads) {
            t.join();
        }

        REQUIRE(runs_completed == num_peers);
    }
}

// =============================================================================
// Slow Shutdown Test - reproduces issue #6 from SYNC_ISSUES_2026-01-28.md
// =============================================================================
//
// 2026-01-29: Test for slow shutdown when protocols are waiting on response channels
//
// Root cause from debug.log analysis:
//   - Ctrl-C at 23:58:00
//   - "Timeout waiting for headers" at 23:58:30 (30 seconds later!)
//   - Shutdown completes at 00:00:00 (2 minutes total)
//
// The problem is that when stop() is called, coroutines waiting on response
// channels with timers (like request_headers) don't get interrupted. They
// wait for the full timeout (30s) before returning.
//
// This test simulates the exact pattern:
//   1. Start peer_session::run()
//   2. Start a coroutine that waits on headers_responses()
//   3. Call stop() while the coroutine is waiting
//   4. Verify shutdown completes in < 2 seconds, not 30 seconds

TEST_CASE("peer_session slow shutdown with pending response channel wait",
          "[peer_session][shutdown][slow]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Simulate a protocol waiting on headers_responses() with a long timeout
    // This is exactly what request_headers() does in protocols_coro.cpp:534-537
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        // Wait for run() to start first
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        // Simulate waiting for headers with 30 second timeout
        // This is the pattern that causes slow shutdown:
        //   co_await (channel.async_receive(...) || timer.async_wait(...))
        // We wrap async_receive to return void to match timer signature
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);  // 30 second timeout like real code

        // Use lambdas that return void to make || operator work
        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->headers_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };

        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());

        // We should get here quickly after stop(), not after 30 seconds
        protocol_completed = true;
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for both run() and protocol to start
    while (!run_started.load() || !protocol_started.load()) {
        std::this_thread::sleep_for(1ms);
    }

    // Give them a moment to settle into their co_await
    std::this_thread::sleep_for(10ms);

    // Now stop the session - this should cause quick termination
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    // Also close the client socket to ensure socket operations complete
    boost_code ignored;
    client.close(ignored);

    // Wait for both coroutines to complete with a reasonable timeout
    // The bug would cause this to take 30 seconds; the fix should make it < 2 seconds
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    // Verify everything completed
    REQUIRE(run_completed);
    REQUIRE(protocol_completed);

    // The critical assertion: shutdown should be fast, not 30 seconds
    // Allow 2 seconds for reasonable completion time
    INFO("Shutdown took " << stop_ms << "ms (should be < 2000ms, bug causes ~30000ms)");
    REQUIRE(stop_ms < 2000);
}

// Additional test with multiple protocols waiting on different channels
TEST_CASE("peer_session slow shutdown with multiple pending protocols",
          "[peer_session][shutdown][slow]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<int> protocols_started{0};
    std::atomic<int> protocols_completed{0};

    constexpr int num_protocols = 3;  // headers, blocks, addr

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Spawn protocol waiting on headers_responses
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->headers_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Spawn protocol waiting on block_responses
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->block_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Spawn protocol waiting on addr_responses
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->addr_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for all to start
    while (!run_started.load() || protocols_started.load() < num_protocols) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(10ms);

    // Stop and measure time
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    boost_code ignored;
    client.close(ignored);

    // Wait for completion
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_completed.load() || protocols_completed.load() < num_protocols) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    REQUIRE(run_completed);
    REQUIRE(protocols_completed == num_protocols);

    INFO("Shutdown with " << num_protocols << " protocols took " << stop_ms << "ms");
    REQUIRE(stop_ms < 2000);
}

// =============================================================================
// Slow Shutdown Test #2 - inbound channel (messages()) not closed
// =============================================================================
//
// 2026-01-29: Test for slow shutdown when protocols are waiting on messages()
//
// Root cause from debug.log analysis:
//   - Ctrl-C at 11:35:00
//   - "Ending protocols for peer" at 11:36:59 (2 minutes later!)
//
// The problem is that run_peer_protocols() in p2p_node.cpp waits on:
//   co_await (peer->messages().async_receive() || ping_timer(2 minutes))
//
// When stop() is called, it closes response channels (headers_responses, etc.)
// but does NOT close the inbound_ channel (messages()). So the coroutine
// waits for the full 2-minute ping timer before exiting.
//
// This test simulates the exact pattern from run_peer_protocols():
//   1. Start peer_session::run()
//   2. Start a coroutine that waits on messages() || timer(2 minutes)
//   3. Call stop() while the coroutine is waiting
//   4. Verify shutdown completes in < 2 seconds, not 2 minutes

TEST_CASE("peer_session slow shutdown with pending messages channel wait",
          "[peer_session][shutdown][slow]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Simulate run_peer_protocols() waiting on messages() || ping_timer
    // This is exactly what p2p_node.cpp does in run_peer_protocols():656-659
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        // Wait for run() to start first
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        // Simulate the ping timer interval (2 minutes in real code)
        // We use 30 seconds here to make the test faster while still demonstrating the bug
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer ping_timer(executor, 30s);

        // This is the pattern from run_peer_protocols() that causes slow shutdown:
        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };

        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await ping_timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());

        // We should get here quickly after stop(), not after 30 seconds
        protocol_completed = true;
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for both run() and protocol to start
    while (!run_started.load() || !protocol_started.load()) {
        std::this_thread::sleep_for(1ms);
    }

    // Give them a moment to settle into their co_await
    std::this_thread::sleep_for(10ms);

    // Now stop the session - this should cause quick termination
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    // Also close the client socket to ensure socket operations complete
    boost_code ignored;
    client.close(ignored);

    // Wait for both coroutines to complete with a reasonable timeout
    // The bug would cause this to take 30 seconds; the fix should make it < 2 seconds
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    // Verify everything completed
    REQUIRE(run_completed);
    REQUIRE(protocol_completed);

    // The critical assertion: shutdown should be fast, not 30 seconds
    INFO("Shutdown took " << stop_ms << "ms (should be < 2000ms, bug causes ~30000ms)");
    REQUIRE(stop_ms < 2000);
}

// =============================================================================
// Multi-threaded shutdown test
// =============================================================================
// This test simulates the REAL production scenario more accurately:
// - Uses a thread_pool with multiple threads (like p2p_node)
// - Runs peer->run() on one executor
// - Runs "protocol" coroutine on pool executor (like run_peer_protocols)
// - Calls stop() from external thread (like executor::stop_async cleanup thread)
//
// The previous test uses single-threaded io_context which may not reproduce
// race conditions that occur in multi-threaded scenarios.

TEST_CASE("peer_session shutdown with multi-threaded executor",
          "[peer_session][shutdown][slow][multithread]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    // Use thread_pool like p2p_node does
    ::asio::thread_pool pool(4);
    auto pool_executor = pool.get_executor();

    // Create a separate io_context for the peer session (like in production)
    ::asio::io_context peer_ctx;

    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(peer_ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    // Start peer_ctx in its own thread (simulates the peer running independently)
    std::thread peer_thread([&peer_ctx]() { peer_ctx.run(); });

    // Start the session's run() loop on peer_ctx
    ::asio::co_spawn(peer_ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Simulate run_peer_protocols() on the POOL executor (different from peer_ctx!)
    // This is key: in production, run_peer_protocols runs on pool_.get_executor(),
    // not on the peer's io_context.
    ::asio::co_spawn(pool_executor, [&]() -> ::asio::awaitable<void> {
        // Wait for run() to start first
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        // Simulate the ping timer (30 seconds to make bug obvious if it exists)
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer ping_timer(executor, 30s);

        // This is the pattern from run_peer_protocols():
        // co_await (peer->messages().async_receive() || ping_timer)
        //
        // NOTE: messages() channel and ping_timer are on DIFFERENT executors!
        // - messages() channel operations go through peer_ctx
        // - ping_timer is on pool_executor
        // This cross-executor || operation may have issues.
        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };

        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await ping_timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());

        protocol_completed = true;
    }, ::asio::detached);

    // Wait for both to start
    auto start_deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_started.load() || !protocol_started.load()) &&
           std::chrono::steady_clock::now() < start_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(run_started);
    REQUIRE(protocol_started);

    // Give them time to settle into their co_await
    std::this_thread::sleep_for(50ms);

    // Now stop from THIS thread (simulates cleanup thread calling stop)
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    // Close client socket
    boost_code ignored;
    client.close(ignored);

    // Wait for completion with timeout
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    // Cleanup
    peer_ctx.stop();
    pool.stop();
    peer_thread.join();
    pool.join();

    // Verify
    INFO("run_completed: " << run_completed.load());
    INFO("protocol_completed: " << protocol_completed.load());
    INFO("Shutdown took " << stop_ms << "ms");

    REQUIRE(run_completed);
    REQUIRE(protocol_completed);
    REQUIRE(stop_ms < 2000);  // Should be fast, not 30 seconds
}

// =============================================================================
// Short Check Interval Test - verifies fix for issue where || operator
// doesn't wake up when channels are closed
// =============================================================================
//
// 2026-01-29: Test for the specific fix that uses short check_interval (5s)
// instead of long ping_interval (2 minutes) for responsive shutdown.
//
// Root cause: In some race conditions, the asio || operator doesn't complete
// when channels are closed. The fix changes run_peer_protocols() to use a
// short check_interval timer instead of long ping_interval, so even if the
// channel close notification doesn't wake up the || operator, the stopped
// flag will be checked within check_interval seconds.
//
// This test simulates the EXACT pattern that was failing:
//   1. Peer dies during normal operation (not during explicit shutdown)
//   2. peer->stop() is called from read_loop error handler
//   3. Channels are closed
//   4. But || operator doesn't wake up
//   5. With the fix, the loop should exit within check_interval (5s)
//   6. Without the fix, it would wait for ping_interval (2 minutes)

TEST_CASE("peer_session check_interval responsive to peer death",
          "[peer_session][shutdown][check_interval]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Simulate run_peer_protocols() with the NEW pattern:
    // - Uses short check_interval (5s) for responsive stopped flag detection
    // - Tracks ping timing separately from the wait loop
    // This matches the fix in p2p_node.cpp::run_peer_protocols()
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        auto executor = co_await ::asio::this_coro::executor;

        // Use SHORT check_interval like the fix (5 seconds)
        // This is the key change: use short timer for responsive shutdown
        constexpr auto check_interval = 5s;

        while (!session->stopped()) {
            ::asio::steady_timer check_timer(executor, check_interval);

            auto receive_op = [&]() -> ::asio::awaitable<void> {
                auto [ec, msg] = co_await session->messages().async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                co_return;
            };

            auto timer_op = [&]() -> ::asio::awaitable<void> {
                co_await check_timer.async_wait(::asio::use_awaitable);
            };

            co_await (receive_op() || timer_op());

            // Check stopped flag after each || iteration
            // This is the key: even if channel close didn't wake up ||,
            // we'll detect stopped within check_interval seconds
            if (session->stopped()) {
                break;
            }
        }

        protocol_completed = true;
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for both to start
    while (!run_started.load() || !protocol_started.load()) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(10ms);

    // Simulate peer death: close the client socket (causes read error)
    // This will trigger peer->stop() from read_loop's error handler
    auto stop_start = std::chrono::steady_clock::now();
    {
        boost_code ignored;
        client.close(ignored);
    }

    // Also call stop explicitly (like supervisor does during shutdown)
    std::this_thread::sleep_for(10ms);
    session->stop(error::service_stopped);

    // Wait for completion
    // With the fix: should complete within check_interval (5s) + overhead
    // Without the fix: would take 2 minutes (ping_interval)
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    // Verify everything completed
    REQUIRE(run_completed);
    REQUIRE(protocol_completed);

    // The critical assertion: shutdown should complete within check_interval + overhead
    // With 5s check_interval, allow up to 7s for safety margin
    INFO("Shutdown took " << stop_ms << "ms (should be < 7000ms with 5s check_interval)");
    REQUIRE(stop_ms < 7000);
}

// Test that verifies the OLD buggy behavior would fail
// This test uses the OLD pattern (long ping_interval without check_interval)
// and demonstrates that it takes too long when channel close doesn't work
// =============================================================================
// Channel cancel() vs close() test - Issue #6 fix verification
// =============================================================================
//
// 2026-02-02: Test for the specific fix where cancel() must be called BEFORE close()
//
// Root cause from debug.log analysis:
//   - peer_tasks.join() hangs indefinitely
//   - 8 peer tasks stuck in run_peer_protocols()
//   - Channels were closed but async_receive() || timer didn't complete
//
// According to Boost.Asio documentation:
//   - close() - marks channel as closed but doesn't cancel pending operations
//   - cancel() - "cancels all asynchronous operations waiting on the channel"
//
// The fix: call cancel() BEFORE close() in peer_session::stop()
//
// This test verifies that the fix works by simulating the exact deadlock scenario

TEST_CASE("peer_session channel cancel before close prevents deadlock",
          "[peer_session][shutdown][cancel]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Simulate run_peer_protocols() - the exact pattern that was deadlocking:
    //   co_await (peer->messages().async_receive() || check_timer)
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        auto executor = co_await ::asio::this_coro::executor;
        // Use 30 second timer to make the bug obvious if cancel() doesn't work
        ::asio::steady_timer check_timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            // This should return with error::channel_cancelled when cancel() is called
            co_return;
        };

        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await check_timer.async_wait(::asio::use_awaitable);
        };

        // This is the || operator pattern that was hanging
        co_await (receive_op() || timer_op());

        protocol_completed = true;
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for both to start
    while (!run_started.load() || !protocol_started.load()) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(50ms);  // Let them settle into co_await

    // Now stop the session
    // The fix: stop() calls cancel() before close() on all channels
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    // Also close client socket
    boost_code ignored;
    client.close(ignored);

    // Wait for completion
    // Bug: would hang for 30 seconds (timer duration)
    // Fix: should complete almost immediately (< 2 seconds)
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    // Verify everything completed
    REQUIRE(run_completed);
    REQUIRE(protocol_completed);

    // Critical assertion: shutdown should be fast, not 30 seconds
    INFO("Shutdown took " << stop_ms << "ms (should be < 2000ms, bug would cause ~30000ms)");
    REQUIRE(stop_ms < 2000);
}

// Test with multiple channels - all should be cancelled properly
TEST_CASE("peer_session all channels cancelled on stop",
          "[peer_session][shutdown][cancel]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<int> protocols_started{0};
    std::atomic<int> protocols_completed{0};

    constexpr int num_protocols = 4;  // messages, headers, blocks, addr

    // Start the session's run() loop
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // Spawn protocol waiting on messages()
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->messages().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Spawn protocol waiting on headers_responses()
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->headers_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Spawn protocol waiting on block_responses()
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->block_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Spawn protocol waiting on addr_responses()
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }
        protocols_started.fetch_add(1);

        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor, 30s);

        auto receive_op = [&]() -> ::asio::awaitable<void> {
            auto [ec, msg] = co_await session->addr_responses().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            co_return;
        };
        auto timer_op = [&]() -> ::asio::awaitable<void> {
            co_await timer.async_wait(::asio::use_awaitable);
        };

        co_await (receive_op() || timer_op());
        protocols_completed.fetch_add(1);
    }, ::asio::detached);

    // Run context in background thread
    std::thread ctx_thread([&ctx]() { ctx.run(); });

    // Wait for all to start
    while (!run_started.load() || protocols_started.load() < num_protocols) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(50ms);

    // Stop and measure time
    auto stop_start = std::chrono::steady_clock::now();
    session->stop(error::service_stopped);

    boost_code ignored;
    client.close(ignored);

    // Wait for completion
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!run_completed.load() || protocols_completed.load() < num_protocols) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    ctx.stop();
    ctx_thread.join();

    REQUIRE(run_completed);
    REQUIRE(protocols_completed == num_protocols);

    INFO("Shutdown with " << num_protocols << " protocols took " << stop_ms << "ms");
    REQUIRE(stop_ms < 2000);
}

TEST_CASE("peer_session OLD pattern without check_interval is slow",
          "[peer_session][shutdown][check_interval][.][bug_demo]") {
    using namespace std::chrono_literals;
    using namespace ::asio::experimental::awaitable_operators;

    // This test is marked [.] so it's hidden by default - run with [bug_demo] tag
    // It demonstrates the BUG in the old code pattern
    // It is NOT expected to pass - it's here to document the bug

    ::asio::io_context ctx;
    auto settings = make_test_settings();

    auto [client, server] = make_connected_sockets(ctx);
    auto session = std::make_shared<peer_session>(std::move(server), settings);

    std::atomic<bool> run_started{false};
    std::atomic<bool> run_completed{false};
    std::atomic<bool> protocol_started{false};
    std::atomic<bool> protocol_completed{false};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        run_started = true;
        co_await session->run();
        run_completed = true;
    }, ::asio::detached);

    // OLD PATTERN: Uses long ping_interval directly, no short check_interval
    // This is the buggy pattern that takes 2 minutes to shutdown
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        while (!run_started.load()) {
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor, 1ms);
            co_await timer.async_wait(::asio::use_awaitable);
        }

        protocol_started = true;

        auto executor = co_await ::asio::this_coro::executor;

        // OLD PATTERN: Long ping_interval (15s for test, would be 2min in prod)
        // Without check_interval, this causes slow shutdown
        constexpr auto ping_interval = 15s;  // Use 15s for test, not 2min

        while (!session->stopped()) {
            ::asio::steady_timer ping_timer(executor, ping_interval);

            auto receive_op = [&]() -> ::asio::awaitable<void> {
                auto [ec, msg] = co_await session->messages().async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                co_return;
            };

            auto timer_op = [&]() -> ::asio::awaitable<void> {
                co_await ping_timer.async_wait(::asio::use_awaitable);
            };

            co_await (receive_op() || timer_op());

            if (session->stopped()) {
                break;
            }
        }

        protocol_completed = true;
    }, ::asio::detached);

    std::thread ctx_thread([&ctx]() { ctx.run(); });

    while (!run_started.load() || !protocol_started.load()) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(10ms);

    auto stop_start = std::chrono::steady_clock::now();
    {
        boost_code ignored;
        client.close(ignored);
    }
    std::this_thread::sleep_for(10ms);
    session->stop(error::service_stopped);

    // Wait with a short timeout - this will likely FAIL because old pattern is slow
    auto deadline = std::chrono::steady_clock::now() + 7s;
    while ((!run_completed.load() || !protocol_completed.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    auto stop_duration = std::chrono::steady_clock::now() - stop_start;
    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_duration).count();

    // Force cleanup
    ctx.stop();
    ctx_thread.join();

    INFO("Shutdown took " << stop_ms << "ms");
    INFO("This test demonstrates the BUG - it should take ~15s (ping_interval)");
    INFO("If channel close works correctly, it would be fast. If not, it's slow.");

    // This assertion may or may not pass depending on whether channel close works
    // The test is here to demonstrate that the old pattern CAN be slow
    // We don't REQUIRE it to fail, but we document that it CAN be slow
    if (stop_ms >= 7000) {
        INFO("OLD PATTERN IS SLOW as expected - took " << stop_ms << "ms");
    } else {
        INFO("Channel close worked correctly this time - took " << stop_ms << "ms");
    }
}
