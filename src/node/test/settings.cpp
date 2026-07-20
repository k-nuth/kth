// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>

using namespace kth;

// Start Test Suite: settings tests

// constructors
//-----------------------------------------------------------------------------

TEST_CASE("settings construct default context expected", "[settings tests]") {
    node::settings configuration;
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("settings construct testnet4 context expected", "[settings tests]") {
    node::settings configuration(domain::config::network::testnet4);
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}

TEST_CASE("settings construct scalenet context expected", "[settings tests]") {
    node::settings configuration(domain::config::network::scalenet);
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}

TEST_CASE("settings construct chipnet context expected", "[settings tests]") {
    node::settings configuration(domain::config::network::chipnet);
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}
#endif

TEST_CASE("settings construct mainnet context expected", "[settings tests]") {
    node::settings configuration(domain::config::network::mainnet);
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}

TEST_CASE("settings construct testnet context expected", "[settings tests]") {
    node::settings configuration(domain::config::network::testnet);
    REQUIRE(configuration.sync_peers == 0u);
    REQUIRE(configuration.sync_timeout_seconds == 5u);
    REQUIRE(configuration.refresh_transactions == true);
}

// End Test Suite
