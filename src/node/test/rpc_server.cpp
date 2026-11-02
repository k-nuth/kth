// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <future>
#include <system_error>
#include <variant>

#include <asio/experimental/awaitable_operators.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <kth/node/rpc/server.hpp>
#include <kth/node/settings.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;

namespace {

// The real JSON-RPC server on a loopback port the system assigns, so a test
// can send the exact bytes an HTTP client puts on the wire and read back what
// the server answers.
struct rpc_server_fixture {
    explicit rpc_server_fixture(char const* tag)
        : chain(tag) {
        settings.bind = "127.0.0.1";
        settings.port = 0;
        settings.user = "test";
        settings.password = "test";
    }

    ~rpc_server_fixture() {
        if (server) server->stop();
        if (runner.joinable()) runner.join();
    }

    void finish() {
        server->stop();
        if (runner.joinable()) runner.join();
        if (server_done.valid()) server_done.get();
    }

    [[nodiscard]] bool start() {
        if ( ! chain.created() || ! chain.start()) return false;
        server = std::make_unique<node::rpc::server>(settings, chain.chain(), io.get_executor());
        server_done = ::asio::co_spawn(io, server->run(), ::asio::use_future);
        auto started = ::asio::co_spawn(io, await_startup(), ::asio::use_future);
        runner = std::thread([this] { io.run(); });
        // Only the synchronous Catch2 driver crosses the executor boundary.
        // Readiness, failure and the deadline suspend coroutines on the I/O loop.
        auto const result = started.get();
        startup_error = result.first;
        port = result.second;
        return !startup_error && port != 0;
    }

    ::asio::awaitable<std::pair<std::error_code, uint16_t>> listening_result() {
        try {
            auto const bound = co_await server->wait_until_listening();
            co_return std::make_pair(std::error_code{}, bound);
        } catch (std::system_error const& e) {
            co_return std::make_pair(e.code(), uint16_t{});
        }
    }

    ::asio::awaitable<std::pair<std::error_code, uint16_t>> await_startup() {
        using namespace ::asio::experimental::awaitable_operators;
        ::asio::steady_timer deadline(io, std::chrono::seconds(5));
        auto result = co_await (listening_result() || deadline.async_wait(::asio::use_awaitable));
        if (result.index() == 0) co_return std::get<0>(result);
        co_return std::make_pair(std::error_code(::asio::error::timed_out), uint16_t{});
    }

    // Send the bytes as given and read until the server closes the connection.
    [[nodiscard]] std::string exchange(std::string const& request) const {
        ::asio::io_context client;
        ::asio::ip::tcp::socket socket(client);
        socket.connect({::asio::ip::make_address("127.0.0.1"), port});
        ::asio::write(socket, ::asio::buffer(request));
        // Nothing more is sent. A server that lost a request then sees EOF and
        // closes, so a missing response fails the count below instead of
        // leaving this read waiting.
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send);
        std::string reply;
        std::error_code ec;
        ::asio::read(socket, ::asio::dynamic_buffer(reply), ec);
        return reply;
    }

    test::chain_fixture chain;
    node::rpc_settings settings;
    ::asio::io_context io;
    std::unique_ptr<node::rpc::server> server;
    std::thread runner;
    std::future<void> server_done;
    std::error_code startup_error;
    uint16_t port = 0;
};

// A POST of getblockcount with Basic auth for test:test.
std::string request(int id, std::string_view connection) {
    auto const body = R"({"jsonrpc":"1.0","id":)" + std::to_string(id)
        + R"(,"method":"getblockcount","params":[]})";
    return "POST / HTTP/1.1\r\n"
           "Host: 127.0.0.1\r\n"
           "Authorization: Basic dGVzdDp0ZXN0\r\n"
           "Content-Type: application/json\r\n"
           "Connection: " + std::string(connection) + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

// The bodies of the HTTP responses in a reply stream, framed by Content-Length.
std::vector<std::string> bodies(std::string_view reply) {
    std::vector<std::string> out;
    constexpr std::string_view length_field = "Content-Length: ";
    while ( ! reply.empty()) {
        auto const end = reply.find("\r\n\r\n");
        if (end == std::string_view::npos) break;
        auto const head = reply.substr(0, end);
        auto const field = head.find(length_field);
        if (field == std::string_view::npos) break;
        auto const length = std::stoul(std::string(head.substr(field + length_field.size())));
        auto const start = end + 4;
        if (reply.size() < start + length) break;
        out.emplace_back(reply.substr(start, length));
        reply.remove_prefix(start + length);
    }
    return out;
}

} // namespace

TEST_CASE("rpc server answers every request pipelined on one connection", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_pipelined");
    REQUIRE(fixture.start());

    // Three requests in one write, the way keep-alive clients pipeline them.
    auto const reply = fixture.exchange(
        request(1, "keep-alive") + request(2, "keep-alive") + request(3, "close"));

    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 3);
    CHECK(answers[0].find(R"("id":1)") != std::string::npos);
    CHECK(answers[1].find(R"("id":2)") != std::string::npos);
    CHECK(answers[2].find(R"("id":3)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server reports bind failure to its startup waiter", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_startup_failure");
    fixture.settings.bind = "not-an-ip-address";
    REQUIRE_FALSE(fixture.start());
    CHECK(fixture.startup_error == ::asio::error::invalid_argument);
    CHECK(fixture.startup_error != ::asio::error::timed_out);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server joins an admitted connection during shutdown", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_join_connection");
    REQUIRE(fixture.start());
    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    socket.connect({::asio::ip::make_address("127.0.0.1"), fixture.port});
    auto const first = request(1, "keep-alive");
    ::asio::write(socket, ::asio::buffer(first));
    std::string reply;
    ::asio::read_until(socket, ::asio::dynamic_buffer(reply), "\r\n\r\n");
    auto const header_end = reply.find("\r\n\r\n") + 4;
    auto const field = reply.find("Content-Length: ");
    REQUIRE(field != std::string::npos);
    auto const length = std::stoul(reply.substr(field + 16));
    if (reply.size() < header_end + length) {
        ::asio::read(socket, ::asio::dynamic_buffer(reply),
            ::asio::transfer_exactly(header_end + length - reply.size()));
    }
    REQUIRE(bodies(reply).size() == 1);
    // An answered request proves admission, rather than just a backlog entry.
    std::string const partial = "POST / HTTP/1.1\r\nHost: localhost\r\n";
    ::asio::write(socket, ::asio::buffer(partial));
    CHECK_NOTHROW(fixture.finish());
    std::array<char, 1> byte{};
    std::error_code ec;
    CHECK(socket.read_some(::asio::buffer(byte), ec) == 0);
    CHECK((ec == ::asio::error::eof || ec == ::asio::error::connection_reset));
}

TEST_CASE("rpc server wakes a startup waiter stopped before run", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_pre_run_stop");
    REQUIRE(fixture.chain.created());
    REQUIRE(fixture.chain.start());
    fixture.server = std::make_unique<node::rpc::server>(
        fixture.settings, fixture.chain.chain(), fixture.io.get_executor());
    auto startup = ::asio::co_spawn(fixture.io, fixture.await_startup(), ::asio::use_future);
    // There is deliberately no run(): stop must settle readiness on its own.
    fixture.server->stop();
    fixture.io.run();
    auto const result = startup.get();
    CHECK(result.first);
    CHECK(result.first != ::asio::error::timed_out);
    CHECK(result.second == 0);
}

TEST_CASE("rpc server accepts request lines that end in a bare LF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_bare_lf");
    REQUIRE(fixture.start());

    // The request as ckpool-family pool software writes it: LF line endings.
    std::string const body = R"({"id":0,"method": "getblockcount"})" "\n";
    auto const reply = fixture.exchange(
        "POST / HTTP/1.1\n"
        "Authorization: Basic dGVzdDp0ZXN0\n"
        "Host: 127.0.0.1:28432\n"
        "Content-type: application/json\n"
        "Connection: close\n"
        "Content-Length: " + std::to_string(body.size()) + "\n"
        "\n" + body);

    REQUIRE(reply.starts_with("HTTP/1.1 200 OK\r\n"));
    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server refuses a CR that does not end its line", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_lone_cr");
    REQUIRE(fixture.start());

    std::string const body = R"({"id":0,"method": "getblockcount"})";
    auto const head_end = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

    // A request line ended by a lone CR, then CRLF headers: mixed framing in
    // which the CR ends no line.
    auto const request_line = fixture.exchange(
        "POST / HTTP/1.1\r"
        "Authorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(request_line.starts_with("HTTP/1.1 400 Bad Request\r\n"));

    // The same inside a header line.
    auto const header_line = fixture.exchange(
        "POST / HTTP/1.1\r\n"
        "Host: 127.0.0.1\rAuthorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(header_line.starts_with("HTTP/1.1 400 Bad Request\r\n"));

    // CRLF throughout is answered, so the refusals above are about the CR.
    auto const crlf = fixture.exchange(
        "POST / HTTP/1.1\r\n"
        "Authorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(crlf.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server validates CR after leading empty lines", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_leading_lines");
    REQUIRE(fixture.start());
    auto malformed = request(1, "close");
    malformed.replace(malformed.find("\r\n"), 2, "\r");
    for (auto const prefix : {"\r\n\r\n", "\n\n"}) {
        auto const reply = fixture.exchange(std::string(prefix) + malformed);
        CHECK(reply.starts_with("HTTP/1.1 400 Bad Request\r\n"));
    }
    auto const valid = fixture.exchange("\r\n\r\n" + request(2, "close"));
    CHECK(valid.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server pipelines LF heads without changing body bytes", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_lf_body");
    REQUIRE(fixture.start());
    auto lf_request = [](std::string const& body, std::string_view connection) {
        return "POST / HTTP/1.1\nAuthorization: Basic dGVzdDp0ZXN0\nConnection: "
            + std::string(connection) + "\nContent-Length: "
            + std::to_string(body.size()) + "\n\n" + body;
    };
    // A lone CR is valid JSON whitespace in the body, not a head terminator.
    std::string const body = "\r" + std::string(R"({"id":7,"method":"getblockcount","params":[]})") + "\r";
    auto const reply = fixture.exchange("\n\n" + lf_request(body, "keep-alive")
        + lf_request(R"({"id":8,"method":"getblockcount","params":[]})", "close"));
    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0].find(R"("id":7)") != std::string::npos);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK(answers[1].find(R"("id":8)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server refuses an unfinished CR at head EOF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_head_eof");
    REQUIRE(fixture.start());
    auto const reply = fixture.exchange("POST / HTTP/1.1\r\nAuthorization: Basic dGVzdDp0ZXN0\r\nContent-Length: 0\r\n\r");
    CHECK(reply.starts_with("HTTP/1.1 400 Bad Request\r\n"));
    CHECK_NOTHROW(fixture.finish());
}
