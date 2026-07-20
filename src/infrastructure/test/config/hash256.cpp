// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

TEST_CASE("hash256::parse_from accepts a 32-byte hex string and round-trips", "[hash256]") {
    auto constexpr text = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
    auto const result = hash256::parse_from(text);
    REQUIRE(result.has_value());
    REQUIRE(result->to_string() == text);
}

TEST_CASE("hash256::parse_from rejects non-hex input", "[hash256]") {
    REQUIRE( ! hash256::parse_from("not a hash").has_value());
}
