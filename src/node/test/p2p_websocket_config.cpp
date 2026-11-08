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
