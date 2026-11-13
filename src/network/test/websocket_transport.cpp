// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <catch2/catch_test_macros.hpp>
#include <kth/network/websocket_transport.hpp>
#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>
#include <asio/use_future.hpp>
#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <span>
#include <vector>

using namespace std::chrono_literals;
using namespace kth;
using namespace kth::network;
using namespace ::asio::experimental::awaitable_operators;

namespace {
// A single io_context runner owns both transport codec and all test operations.
// TCP setup uses the listening backlog; no detached test threads exist.
struct sockets {
    ::asio::io_context context;
    ::asio::ip::tcp::socket client{context};
    ::asio::ip::tcp::socket server{context};
    sockets() {
        ::asio::ip::tcp::acceptor acceptor(context, {::asio::ip::tcp::v4(), 0});
        client.connect(acceptor.local_endpoint());
        server = acceptor.accept();
    }
    void run(::asio::awaitable<void> task) {
        auto guarded = [&]() -> ::asio::awaitable<void> {
            ::asio::steady_timer limit(context);
            limit.expires_after(5s);
            auto result = co_await (std::move(task) || limit.async_wait(::asio::as_tuple(::asio::use_awaitable)));
            REQUIRE(result.index() == 0);
        };
        auto future = ::asio::co_spawn(context, guarded(), ::asio::use_future);
        context.run();
        future.get();
    }
};

websocket_server_options options() {
    return {.path = "/p2p", .origins = {"http://test.local"}, .handshake_timeout = 1s};
}

::asio::awaitable<void> upgrade(::asio::ip::tcp::socket& client) {
    std::string request = "GET /p2p HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://test.local\r\n\r\n";
    co_await ::asio::async_write(client, ::asio::buffer(request), ::asio::use_awaitable);
    std::string response;
    co_await ::asio::async_read_until(client, ::asio::dynamic_buffer(response, 8192), "\r\n\r\n", ::asio::use_awaitable);
    REQUIRE(response.starts_with("HTTP/1.1 101 "));
    REQUIRE(response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
}

// Test-only RFC6455 masked frame builder: tiny payloads are enough to exercise
// fragmentation/control semantics without depending on a second WS library.
data_chunk frame(uint8_t opcode, bool final, std::span<uint8_t const> bytes) {
    REQUIRE(bytes.size() < 126);
    std::array<uint8_t, 4> mask{0x11, 0x22, 0x33, 0x44};
    data_chunk wire{static_cast<uint8_t>((final ? 0x80 : 0) | opcode), static_cast<uint8_t>(0x80 | bytes.size())};
    wire.insert(wire.end(), mask.begin(), mask.end());
    for (size_t i = 0; i < bytes.size(); ++i) wire.push_back(bytes[i] ^ mask[i % 4]);
    return wire;
}

struct wire_message { uint8_t opcode; data_chunk payload; };
::asio::awaitable<wire_message> receive(::asio::ip::tcp::socket& client) {
    std::array<uint8_t, 2> head;
    co_await ::asio::async_read(client, ::asio::buffer(head), ::asio::use_awaitable);
    REQUIRE((head[0] & 0x80) != 0);
    REQUIRE((head[1] & 0x80) == 0); // Server frames must be unmasked.
    uint64_t length = head[1] & 0x7f;
    if (length == 126) {
        std::array<uint8_t, 2> extended;
        co_await ::asio::async_read(client, ::asio::buffer(extended), ::asio::use_awaitable);
        length = (uint64_t{extended[0]} << 8) | extended[1];
    } else if (length == 127) {
        std::array<uint8_t, 8> extended;
        co_await ::asio::async_read(client, ::asio::buffer(extended), ::asio::use_awaitable);
        length = 0;
        for (auto byte : extended) length = (length << 8) | byte;
    }
    REQUIRE(length <= 65536);
    data_chunk bytes(static_cast<size_t>(length));
    co_await ::asio::async_read(client, ::asio::buffer(bytes), ::asio::use_awaitable);
    co_return wire_message{static_cast<uint8_t>(head[0] & 0xf), std::move(bytes)};
}
}

TEST_CASE("WebSocket transport joins binary fragments while answering interleaved ping", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto accepted = co_await (transport.accept(options()) && upgrade(pair.client));
        REQUIRE(!accepted);
        auto scenario = [&]() -> ::asio::awaitable<void> {
            data_chunk first{1, 2};
            data_chunk ping{9, 8, 7};
            data_chunk last{3, 4, 5};
            auto wire = frame(2, false, first);
            auto control = frame(9, true, ping);
            auto continuation = frame(0, true, last);
            wire.insert(wire.end(), control.begin(), control.end());
            wire.insert(wire.end(), continuation.begin(), continuation.end());
            co_await ::asio::async_write(pair.client, ::asio::buffer(wire), ::asio::use_awaitable);
            std::array<uint8_t, 5> buffer{};
            auto [ec, count] = co_await transport.read_exact(::asio::buffer(buffer));
            REQUIRE(!ec);
            REQUIRE(count == buffer.size());
            REQUIRE((buffer == std::array<uint8_t, 5>{1,2,3,4,5}));
            auto pong = co_await receive(pair.client);
            REQUIRE(pong.opcode == 10);
            REQUIRE(pong.payload == ping);
            transport.stop();
        };
        co_await (transport.run() && scenario());
    };
    pair.run(task());
}

TEST_CASE("WebSocket messages beyond the decoded queue wait for the reader", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto accepted = co_await (transport.accept(options()) && upgrade(pair.client));
        REQUIRE(!accepted);
        auto scenario = [&]() -> ::asio::awaitable<void> {
            // 64 one-byte messages in one TCP write, far more than the decoded
            // queue holds, before anything reads them: the codec stops reading
            // the socket until there is room, and every byte arrives in order.
            data_chunk wire;
            for (uint8_t i = 0; i < 64; ++i) {
                std::array<uint8_t, 1> payload{i};
                auto part = frame(2, true, payload);
                wire.insert(wire.end(), part.begin(), part.end());
            }
            co_await ::asio::async_write(pair.client, ::asio::buffer(wire), ::asio::use_awaitable);
            ::asio::steady_timer later(pair.context);
            later.expires_after(50ms);
            co_await later.async_wait(::asio::use_awaitable);
            std::array<uint8_t, 64> buffer{};
            auto [ec, count] = co_await transport.read_exact(::asio::buffer(buffer));
            REQUIRE(!ec);
            REQUIRE(count == buffer.size());
            for (uint8_t i = 0; i < 64; ++i) REQUIRE(buffer[i] == i);
            transport.stop();
        };
        co_await (transport.run() && scenario());
    };
    pair.run(task());
}

TEST_CASE("WebSocket control replies are not held behind data waiting for the reader", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto accepted = co_await (transport.accept(options()) && upgrade(pair.client));
        REQUIRE(!accepted);
        auto scenario = [&]() -> ::asio::awaitable<void> {
            // Six data messages, more than the decoded queue holds, then a
            // ping, before anything reads the data: the pong is answered while
            // the data waits for the reader.
            data_chunk wire;
            for (uint8_t i = 0; i < 6; ++i) {
                std::array<uint8_t, 1> payload{i};
                auto part = frame(2, true, payload);
                wire.insert(wire.end(), part.begin(), part.end());
            }
            data_chunk ping{4, 2};
            auto control = frame(9, true, ping);
            wire.insert(wire.end(), control.begin(), control.end());
            co_await ::asio::async_write(pair.client, ::asio::buffer(wire), ::asio::use_awaitable);
            auto pong = co_await receive(pair.client);
            REQUIRE(pong.opcode == 10);
            REQUIRE(pong.payload == ping);
            std::array<uint8_t, 6> buffer{};
            auto [ec, count] = co_await transport.read_exact(::asio::buffer(buffer));
            REQUIRE(!ec);
            REQUIRE((buffer == std::array<uint8_t, 6>{0, 1, 2, 3, 4, 5}));
            transport.stop();
        };
        co_await (transport.run() && scenario());
    };
    pair.run(task());
}

TEST_CASE("WebSocket stop before codec pump starts leaves no suspended tasks", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto accepted = co_await (transport.accept(options()) && upgrade(pair.client));
        REQUIRE(!accepted);
        transport.stop();
        co_await transport.run();
        std::array<uint8_t, 1> byte;
        auto [ec, count] = co_await pair.client.async_read_some(::asio::buffer(byte), ::asio::as_tuple(::asio::use_awaitable));
        REQUIRE(ec == ::asio::error::eof);
        REQUIRE(count == 0);
    };
    pair.run(task());
}

TEST_CASE("WebSocket HTTP deadline cancels and joins a silent handshake", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto ec = co_await transport.accept(options());
        REQUIRE(ec == ::asio::error::timed_out);
        transport.stop();
    };
    pair.run(task());
}

TEST_CASE("WebSocket stop wakes and joins a pending partial HTTP upgrade", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto partial_then_stop = [&]() -> ::asio::awaitable<void> {
            std::string partial = "GET /p2p HTTP/1.1\r\nHost: localhost\r\n";
            co_await ::asio::async_write(pair.client, ::asio::buffer(partial), ::asio::use_awaitable);
            transport.stop();
        };
        auto result = co_await (transport.accept(options()) && partial_then_stop());
        REQUIRE((result == ::asio::error::operation_aborted || result == ::asio::error::eof));
    };
    pair.run(task());
}

TEST_CASE("WebSocket writes split a large BCH byte stream into bounded binary messages", "[websocket_transport]") {
    sockets pair;
    websocket_transport transport(pair.server);
    auto task = [&]() -> ::asio::awaitable<void> {
        auto accepted = co_await (transport.accept(options()) && upgrade(pair.client));
        REQUIRE(!accepted);
        data_chunk payload(2 * 65536 + 7);
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
        auto read = [&]() -> ::asio::awaitable<data_chunk> {
            data_chunk received;
            while (received.size() < payload.size()) {
                auto message = co_await receive(pair.client);
                REQUIRE(message.opcode == 2);
                REQUIRE(message.payload.size() <= 65536);
                received.insert(received.end(), message.payload.begin(), message.payload.end());
            }
            co_return received;
        };
        auto scenario = [&]() -> ::asio::awaitable<void> {
            auto [written, received] = co_await (transport.write_exact(::asio::buffer(payload)) && read());
            REQUIRE(!written.first);
            REQUIRE(written.second == payload.size());
            REQUIRE(received == payload);
            transport.stop();
        };
        co_await (transport.run() && scenario());
    };
    pair.run(task());
}

namespace {
// An upgrade request with extra header lines; the status line of the answer.
::asio::awaitable<std::string> upgrade_with(::asio::ip::tcp::socket& client, std::string const& extra) {
    std::string request = "GET /p2p HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://test.local\r\n" + extra + "\r\n";
    co_await ::asio::async_write(client, ::asio::buffer(request), ::asio::use_awaitable);
    std::string response;
    co_await ::asio::async_read_until(client, ::asio::dynamic_buffer(response, 8192), "\r\n\r\n", ::asio::use_awaitable);
    co_return response.substr(0, response.find("\r\n"));
}

struct upgrade_outcome {
    std::error_code accepted;
    std::string status;
    std::optional<::asio::ip::address> client;
};

upgrade_outcome try_upgrade(bool trusted, std::string const& extra) {
    sockets pair;
    websocket_transport transport(pair.server);
    auto settings = options();
    if (trusted) settings.trusted_proxies = {::asio::ip::make_address("127.0.0.1")};
    upgrade_outcome outcome;
    auto task = [&]() -> ::asio::awaitable<void> {
        auto [accepted, status] = co_await (transport.accept(settings) && upgrade_with(pair.client, extra));
        outcome.accepted = accepted;
        outcome.status = status;
        outcome.client = transport.forwarded_client();
    };
    pair.run(task());
    return outcome;
}
}

TEST_CASE("WebSocket clients behind a trusted proxy are named by the address it appended", "[websocket_transport]") {
    // Through the proxy: the last X-Forwarded-For entry, the one it appended.
    auto proxied = try_upgrade(true, "X-Forwarded-For: 198.51.100.9, 203.0.113.7\r\n");
    REQUIRE(!proxied.accepted);
    REQUIRE(proxied.status.starts_with("HTTP/1.1 101"));
    REQUIRE(proxied.client == ::asio::ip::make_address("203.0.113.7"));

    // Through the proxy without the header, or with garbage: refused.
    auto unnamed = try_upgrade(true, "");
    REQUIRE(unnamed.accepted == ::asio::error::access_denied);
    REQUIRE(unnamed.status.starts_with("HTTP/1.1 403"));
    REQUIRE(try_upgrade(true, "X-Forwarded-For: not-an-address\r\n").accepted == ::asio::error::access_denied);

    // A direct client cannot name itself: the header is ignored.
    auto direct = try_upgrade(false, "X-Forwarded-For: 203.0.113.7\r\n");
    REQUIRE(!direct.accepted);
    REQUIRE(!direct.client);
}
