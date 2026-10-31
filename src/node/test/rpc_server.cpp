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

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <kth/node/rpc/server.hpp>
#include <kth/node/settings.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;

namespace {

// The real JSON-RPC server on a loopback port, so a test can send the exact
// bytes an HTTP client puts on the wire and read back what the server answers.
struct rpc_server_fixture {
    rpc_server_fixture(char const* tag, uint16_t port)
        : chain(tag) {
        settings.bind = "127.0.0.1";
        settings.port = port;
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
        ::asio::co_spawn(io, server->run(), ::asio::detached);
        runner = std::thread([this] { io.run(); });
        // The listener binds on the server's own strand; wait until it accepts.
        for (int attempt = 0; attempt < 200; ++attempt) {
            ::asio::io_context probe;
            ::asio::ip::tcp::socket socket(probe);
            std::error_code ec;
            socket.connect({::asio::ip::make_address("127.0.0.1"), settings.port}, ec);
            if ( ! ec) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    // Send the bytes as given and read until the server closes the connection.
    [[nodiscard]] std::string exchange(std::string const& request) const {
        ::asio::io_context client;
        ::asio::ip::tcp::socket socket(client);
        socket.connect({::asio::ip::make_address("127.0.0.1"), settings.port});
        ::asio::write(socket, ::asio::buffer(request));
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
    rpc_server_fixture fixture("rpc_server_pipelined", 28431);
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
