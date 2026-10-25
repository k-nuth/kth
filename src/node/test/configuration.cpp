// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>
#include <sstream>

using namespace kth;

TEST_CASE("parser retains explicit settings after network defaults are rebuilt", "[configuration tests][parser]") {
    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--db.directory", "selected-database",
        "--rpc.enabled=true", "--net.outbound_connections", "0"};
    std::ostringstream error;
    REQUIRE(parser.parse(8, argv, error));
    CHECK(parser.configured.database.directory == path{"selected-database"});
    CHECK(parser.configured.rpc.enabled);
    CHECK(parser.configured.network.outbound_connections == 0);
}

#if !defined(KTH_DB_READONLY)
TEST_CASE("offline compaction flag survives network selection and excludes initialization", "[configuration tests][parser]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--compact-utxo"};
    std::ostringstream error;
    REQUIRE(parser.parse(4, argv, error));
    CHECK(parser.configured.compact_utxo);
    node::parser conflicting(domain::config::network::mainnet);
    char const* invalid[]{"kth", "--compact-utxo", "--init_run"};
    CHECK_FALSE(conflicting.parse(3, invalid, error));
}
#endif

// Start Test Suite: configuration tests

// constructors
//-----------------------------------------------------------------------------

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("configuration construct1 testnet4 context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::testnet4);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 scalenet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::scalenet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 chipnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::chipnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}
#endif

TEST_CASE("configuration construct1 mainnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::mainnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 testnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::testnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("configuration construct2 testnet4 context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::testnet4);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}

TEST_CASE("configuration construct2 scalenet context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::scalenet);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}

TEST_CASE("configuration construct2 chipnet context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::chipnet);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}
#endif

// End Test Suite
