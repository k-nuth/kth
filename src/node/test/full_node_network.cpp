// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test_helpers.hpp>
#include <kth/node.hpp>
#include <chrono>
#include <sstream>

using namespace kth;

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("full node retains explicit chipnet context when the port is customized", "[node][network_context]") {
    node::parser parser(domain::config::network::mainnet);
    std::ostringstream errors;
    char const* argv[]{"kth", "--network", "chipnet", "--net.inbound_port", "12345"};
    REQUIRE(parser.parse(5, argv, errors));
    auto config = parser.configured;
    struct owned_directory {
        path directory = std::filesystem::temp_directory_path() /
            ("kth_full_node_network_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = std::filesystem::create_directory(directory);
        ~owned_directory() { if (owned) { std::error_code ec; std::filesystem::remove_all(directory, ec); } }
    } storage;
    REQUIRE(storage.owned);
    config.database.directory = storage.directory;
    {
        database::data_base db(config.database);
        REQUIRE(db.create(node::full_node::get_genesis_block(config.net)));
    }
    node::full_node instance(config);
    REQUIRE(instance.chain().start(get_disk_magic(config.net)));
    auto const state = instance.chain().chain_state();
    REQUIRE(state);
    CHECK(state->network() == config.net);
}

#endif
