// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: ec private tests

// TODO(legacy): add version tests

constexpr auto secret = "8010b1bb119ad37d4b65a1022a314897b1b3614b345974332cb1b9582cf03536"_base16;
constexpr auto wif_compressed_str = "L1WepftUBemj6H4XQovkiW1ARVjxMqaw4oj2kmkYqdG1xTnBcHfC";
constexpr auto wif_uncompressed_str = "5JngqQmHagNTknnCshzVUysLMWAjT23FWs1TgNU5wyFH5SB3hrP";

TEST_CASE("ec private compressed wif compressed test", "[ec private]") {
    auto const key = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh);
    REQUIRE(key);
    REQUIRE(key->compressed());
}

TEST_CASE("ec private uncompressed wif not compressed test", "[ec private]") {
    auto const key = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh);
    REQUIRE(key);
    REQUIRE( ! key->compressed());
}

TEST_CASE("ec private encode wif compressed test", "[ec private]") {
    REQUIRE(ec_private::from_verified_secret(secret, ec_private::mainnet, true).to_string() == wif_compressed_str);
}

TEST_CASE("ec private encode wif uncompressed test", "[ec private]") {
    REQUIRE(ec_private::from_verified_secret(secret, 0x8000, false).to_string() == wif_uncompressed_str);
}

TEST_CASE("ec private decode wif compressed test", "[ec private]") {
    auto const priv = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh);
    REQUIRE(priv);
    REQUIRE(priv->secret() == secret);
    REQUIRE(priv->version() == 0x8000);
    REQUIRE(priv->compressed());
}

TEST_CASE("ec private decode wif uncompressed test", "[ec private]") {
    auto const priv = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh);
    REQUIRE(priv);
    REQUIRE(priv->secret() == secret);
    REQUIRE(priv->version() == 0x8000);
    REQUIRE( ! priv->compressed());
}

// End Test Suite
