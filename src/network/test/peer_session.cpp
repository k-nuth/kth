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
    auto const size_le = to_little_endian(static_cast<uint32_t>(payload.size()));
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
    ctx.run_for(10ms);

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
    ctx.run_for(10ms);

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
    ctx.run_for(10ms);

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
    ctx.run_for(10ms);

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
    ctx.run_for(10ms);

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
    ctx.run_for(10ms);

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
        // session destructor called here when shared_ptr ref count goes to 0
    }

    // Context should be able to complete without hanging
    ctx.run_for(50ms);
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
