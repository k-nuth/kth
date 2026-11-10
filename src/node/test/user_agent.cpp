// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license. See COPYING or LICENSE.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <kth/domain/version.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/user_agent.hpp>
#include <kth/node/parser.hpp>
#include <kth/network/protocols_coro.hpp>
#include <sstream>

#include <chrono>
#include <filesystem>
#include <format>

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("node user agent does not advertise the configured block size", "[node][user_agent]") {
    auto const net = GENERATE(kth::domain::config::network::mainnet,
                             kth::domain::config::network::tempnet);
    struct owned_directory {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kth_user_agent_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = std::filesystem::create_directory(directory);
        ~owned_directory() {
            if (owned) {
                std::error_code ec;
                std::filesystem::remove_all(directory, ec);
            }
        }
    } storage;
    REQUIRE(storage.owned);
    kth::node::configuration config(net);
    config.database.directory = storage.directory;
    config.network.peers_file = storage.directory / "peers.dat";
    config.network.hosts_file = storage.directory / "hosts.dat";
    config.network.banlist_file = storage.directory / "bans.dat";
    config.network.threads = 1;
    config.chain.cores = 1;
    kth::node::full_node node(config);
    CHECK(node.network_settings().user_agent ==
          std::format("/{}:{}/", kth::client_name, kth::version));
}

#endif

TEST_CASE("user agent comments retain their separators", "[node][user_agent]") {
    CHECK(kth::node::get_user_agent({"example", "other"}) ==
          std::format("/{}:{}(example; other)/", kth::client_name, kth::version));
}

TEST_CASE("legacy discovery is an explicit configuration option", "[node][ws_negotiation]") {
    kth::node::parser parser(kth::domain::config::network::regtest);
    CHECK_FALSE(parser.configured.network.ws_discovery_legacy_agent);
    char const* argv[]{"kth", "--network", "regtest", "--net.ws_discovery", "true",
                       "--net.ws_discovery_legacy_agent", "true"};
    std::ostringstream errors;
    REQUIRE(parser.parse(7, argv, errors));
    CHECK(parser.configured.network.ws_discovery);
    CHECK(parser.configured.network.ws_discovery_legacy_agent);
    auto settings = parser.configured.network;
    settings.user_agent = "/KnuthTest:1/";
    CHECK(kth::network::make_handshake_config(settings, 0, 1).user_agent == "/KnuthTest:1/wsdisc:1/");
    settings.ws_discovery = false;
    CHECK(kth::network::make_handshake_config(settings, 0, 1).user_agent == "/KnuthTest:1/");
}
