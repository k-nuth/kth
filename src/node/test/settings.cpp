// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>

using namespace kth;

// Start Test Suite: settings tests

// constructors
//-----------------------------------------------------------------------------

TEST_CASE("sv2 settings default to disabled localhost listener with Noise", "[settings tests][sv2 noise]") {
    node::sv2_settings const configuration;
    REQUIRE_FALSE(configuration.enabled);
    REQUIRE(configuration.bind == "127.0.0.1");
    REQUIRE(configuration.port == 8442u);
    REQUIRE(configuration.noise);
    REQUIRE(configuration.static_key_file == ".sv2-static-key");
    REQUIRE(configuration.authority_key_file == ".sv2-authority-key");
    REQUIRE(configuration.certificate_file.empty());
    REQUIRE(configuration.authority_public_key.empty());
    REQUIRE(configuration.certificate_validity_seconds == 2592000u);
    REQUIRE(configuration.handshake_timeout_seconds == 10u);
}

TEST_CASE("sv2 mining defaults to disabled without a payout address", "[settings tests][sv2 mining]") {
    node::sv2_settings const configuration;
    CHECK_FALSE(configuration.mining.enabled);
    CHECK(configuration.mining.address.empty());
}

TEST_CASE("sv2 Job Declaration defaults to disabled without upstreams or a mode", "[settings tests][sv2 jd]") {
    node::sv2_settings const configuration;
    CHECK_FALSE(configuration.jd.enabled);
    CHECK(configuration.jd.mining_address.empty());
    CHECK(configuration.jd.mining_authority_public_key.empty());
    CHECK(configuration.jd.jds_address.empty());
    CHECK(configuration.jd.jds_authority_public_key.empty());
    CHECK(configuration.jd.user_identity.empty());
    CHECK(configuration.jd.mode == "coinbase-only");
    CHECK(configuration.jd.response_timeout_seconds == 10u);
}

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
