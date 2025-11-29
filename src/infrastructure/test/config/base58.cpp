// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

constexpr auto base58_encoded_a = "vYxp6yFC7qiVtK1RcGQQt3L6EqTc8YhEDLnSMLqDvp8D";
constexpr auto base58_decoded_a = std::to_array<uint8_t>({
    0x03, 0x1b, 0xab, 0x84, 0xe6, 0x87, 0xe3, 0x65, 0x14, 0xee, 0xaf, 0x5a,
    0x01, 0x7c, 0x30, 0xd3, 0x2c, 0x1f, 0x59, 0xdd, 0x4e, 0xa6, 0x62, 0x9d,
    0xa7, 0x97, 0x0c, 0xa3, 0x74, 0x51, 0x3d, 0xd0, 0x06
});

// Start Test Suite: base58 tests

TEST_CASE("base58 default constructor does not throw", "[base58]") {
    REQUIRE_NOTHROW(base58());
}

TEST_CASE("base58 from_string valid string decodes", "[base58]") {
    auto const result = base58::from_string(base58_encoded_a);
    REQUIRE(result.has_value());
    REQUIRE(std::ranges::equal(base58_decoded_a, result->data()));
}

TEST_CASE("base58 from_string invalid string returns error", "[base58]") {
    auto const result = base58::from_string("bo-gus");
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
}

TEST_CASE("base58 round trip from_string to_string", "[base58]") {
    auto const result = base58::from_string(base58_encoded_a);
    REQUIRE(result.has_value());
    REQUIRE(result->to_string() == base58_encoded_a);
}

// End Test Suite
