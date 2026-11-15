// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#include <test_helpers.hpp>
#include <kth/node/parser.hpp>
#include <kth/node/p2p_node.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <sstream>

using namespace kth;

namespace {
code start_ws_test(network::settings& settings) {
    settings.outbound_connections = 0;
    settings.host_pool_capacity = 0;
    settings.seeds.clear();
    settings.peers_file.clear();
    settings.hosts_file.clear();
    settings.banlist_file.clear();
    node::p2p_node node(settings);
    ::asio::io_context context;
    auto started = ::asio::co_spawn(context, node.start(), ::asio::use_future);
    context.run();
    auto result = started.get();
    node.stop();
    node.join();
    return result;
}
}

TEST_CASE("WebSocket runtime settings survive network default reconstruction", "[websocket][configuration]") {
    node::parser parser(domain::config::network::regtest);
    char const* arguments[]{"kth", "--network", "regtest", "--net.ws_enabled=true",
        "--net.ws_port", "19080", "--net.ws_origins", "https://wallet.example.org",
        "--net.ws_discovery=true", "--net.ws_public_endpoints", "wss://peer.example.org/p2p"};
    std::ostringstream errors;
    REQUIRE(parser.parse(11, arguments, errors));
    CHECK(parser.configured.network.ws_enabled);
    CHECK(parser.configured.network.ws_port == 19080);
    CHECK(parser.configured.network.ws_discovery);
    REQUIRE(parser.configured.network.ws_public_endpoints.size() == 1);
    CHECK(parser.configured.network.ws_public_endpoints.front() == "wss://peer.example.org/p2p");
    CHECK(parser.configured.network.ws_origins == std::vector<std::string>{"https://wallet.example.org"});
}

TEST_CASE("WebSocket activation is explicit and unsupported builds refuse it", "[websocket][configuration]") {
    network::settings settings(domain::config::network::regtest);
    settings.ws_enabled = true;
    settings.ws_origins = {"https://wallet.example.org"};
#if defined(KTH_WITH_WEBSOCKET)
    CHECK(start_ws_test(settings) == error::success);
#else
    CHECK(start_ws_test(settings) == error::operation_failed);
#endif
    settings.ws_enabled = false;
    CHECK(start_ws_test(settings) == error::success);
}

TEST_CASE("WebSocket public advertisement rejects private or malformed configured endpoints", "[websocket][configuration]") {
    for (auto const* url : {"ws://127.0.0.1:8080/p2p", "wss://10.0.0.1/p2p", "https://peer.example.org/p2p"}) {
        network::settings settings(domain::config::network::regtest);
        settings.ws_discovery = true;
        settings.ws_public_endpoints = {url};
        CHECK(start_ws_test(settings) == error::operation_failed);
    }
    network::settings lan(domain::config::network::regtest);
    lan.ws_discovery = true;
    lan.ws_allow_private = true;
    lan.ws_public_endpoints = {"ws://127.0.0.1:8080/p2p"};
    CHECK(start_ws_test(lan) == error::success);
    lan.ws_discovery = false;
    CHECK(start_ws_test(lan) == error::operation_failed);
}

#if defined(KTH_WITH_WEBSOCKET) && defined(__linux__)
#include <asio/connect.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>
#include <filesystem>
#include <set>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <vector>

namespace {
// The lowest descriptor number not in use (the listing's own counts as used):
// with the soft limit there, at most the holes below it remain to be taken.
rlim_t lowest_free_descriptor() {
    std::set<int> open;
    for (auto const& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        open.insert(std::stoi(entry.path().filename().string()));
    }
    int lowest = 0;
    while (open.contains(lowest)) ++lowest;
    return rlim_t(lowest);
}
}

TEST_CASE("WebSocket listener survives a failed accept", "[websocket][configuration]") {
    ::asio::io_context context;
    uint16_t port = 0;
    {
        ::asio::ip::tcp::acceptor probe(context, {::asio::ip::address_v4::loopback(), 0});
        port = probe.local_endpoint().port();
    }
    network::settings settings(domain::config::network::regtest);
    settings.outbound_connections = 0;
    settings.inbound_port = 0;
    settings.host_pool_capacity = 0;
    settings.seeds.clear();
    settings.peers_file.clear();
    settings.hosts_file.clear();
    settings.banlist_file.clear();
    settings.ws_enabled = true;
    settings.ws_port = port;
    settings.ws_origins = {"http://test.local"};
    node::p2p_node node(settings);

    bool survived = false;
    std::string status;
    auto scenario = [&]() -> ::asio::awaitable<void> {
        using namespace std::chrono_literals;
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer pause(executor);
        pause.expires_after(200ms);
        co_await pause.async_wait(::asio::use_awaitable);

        // The client's descriptor exists before the limit drops; the
        // listener's accept then has none to take, and fails (EMFILE).
        ::asio::ip::tcp::socket client(executor);
        client.open(::asio::ip::tcp::v4());
        rlimit original{};
        REQUIRE(getrlimit(RLIMIT_NOFILE, &original) == 0);
        rlimit lowered = original;
        lowered.rlim_cur = lowest_free_descriptor();
        REQUIRE(setrlimit(RLIMIT_NOFILE, &lowered) == 0);
        // Any descriptor still free below the limit is taken, so that none is left.
        std::vector<int> fillers;
        for (int descriptor; (descriptor = ::socket(AF_INET, SOCK_STREAM, 0)) >= 0;) fillers.push_back(descriptor);
        REQUIRE(errno == EMFILE);
        co_await client.async_connect({::asio::ip::address_v4::loopback(), port}, ::asio::use_awaitable);
        pause.expires_after(300ms);
        co_await pause.async_wait(::asio::use_awaitable);
        REQUIRE(setrlimit(RLIMIT_NOFILE, &original) == 0);
        for (auto descriptor : fillers) ::close(descriptor);
        survived = !node.stopped();

        // With descriptors back, the same listener upgrades the waiting client.
        std::string request = "GET /p2p HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://test.local\r\n\r\n";
        co_await ::asio::async_write(client, ::asio::buffer(request), ::asio::use_awaitable);
        std::string response;
        co_await ::asio::async_read_until(client, ::asio::dynamic_buffer(response, 8192), "\r\n\r\n", ::asio::use_awaitable);
        status = response.substr(0, response.find("\r\n"));
        node.stop();
    };
    auto run = [&]() -> ::asio::awaitable<void> {
        REQUIRE(co_await node.start() == error::success);
        using namespace ::asio::experimental::awaitable_operators;
        co_await (node.run() && scenario());
    };
    auto done = ::asio::co_spawn(context, run(), ::asio::use_future);
    context.run();
    done.get();
    node.join();
    CHECK(survived);
    CHECK(status.starts_with("HTTP/1.1 101"));
}
#endif
