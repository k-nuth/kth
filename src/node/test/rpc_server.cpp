// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <atomic>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
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

    [[nodiscard]] bool start() {
        if ( ! chain.created() || ! chain.start()) return false;
        server = std::make_unique<node::rpc::server>(settings, chain.chain(), io.get_executor());
        ::asio::co_spawn(io, server->run(), [this](std::exception_ptr) { ended = true; });
        runner = std::thread([this] { io.run(); });
        // The listener binds on the server's own strand. Wait for the server to
        // report the port it listens on, or for run() to end, as it does when
        // the bind fails.
        for (int attempt = 0; attempt < 500; ++attempt) {
            if (auto const bound = server->listening_port(); bound != 0) {
                port = bound;
                return true;
            }
            if (ended) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
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
    std::atomic<bool> ended{false};
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
}

TEST_CASE("rpc server replies end their headers with Content-Length and their body with LF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_reply_framing");
    REQUIRE(fixture.start());

    // The header line right before the blank line, as a line reader sees it.
    auto last_header = [](std::string_view reply) {
        auto const end = reply.find("\r\n\r\n");
        auto const head = reply.substr(0, end);
        return std::string(head.substr(head.rfind("\r\n") + 2));
    };

    auto const ok = fixture.exchange(request(1, "close"));
    auto const answers = bodies(ok);
    REQUIRE(answers.size() == 1);
    CHECK(last_header(ok).starts_with("Content-Length: "));
    CHECK(answers[0].ends_with("}\n"));

    // An error reply with an extra header is framed the same way.
    auto unauthorized = request(2, "close");
    unauthorized.erase(unauthorized.find("Authorization:"), std::string("Authorization: Basic dGVzdDp0ZXN0\r\n").size());
    auto const denied = fixture.exchange(unauthorized);
    REQUIRE(denied.starts_with("HTTP/1.1 401 "));
    CHECK(last_header(denied).starts_with("Content-Length: "));
    REQUIRE(bodies(denied).size() == 1);
    CHECK(bodies(denied)[0].ends_with("}\n"));
}
