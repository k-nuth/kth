// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <limits>
#include <utility>

#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: limits tests

// safe_unsigned compares against numeric_limits constants and returns an
// error code instead of throwing.
static_assert(noexcept(safe_unsigned<uint32_t>(std::declval<uint64_t>())));
static_assert(noexcept(safe_unsigned<uint8_t>(std::declval<uint32_t>())));
static_assert(noexcept(safe_unsigned<uint64_t>(std::declval<uint32_t>())));

TEST_CASE("limits - safe_unsigned accepts values in range", "[limits tests]") {
    auto const maximum = safe_unsigned<uint32_t>(uint64_t{std::numeric_limits<uint32_t>::max()});
    REQUIRE(maximum.has_value());
    REQUIRE(*maximum == std::numeric_limits<uint32_t>::max());

    auto const zero = safe_unsigned<uint8_t>(uint32_t{0});
    REQUIRE(zero.has_value());
    REQUIRE(*zero == 0u);

    auto const widened = safe_unsigned<uint64_t>(uint32_t{7});
    REQUIRE(widened.has_value());
    REQUIRE(*widened == 7u);
}

TEST_CASE("limits - safe_unsigned reports out_of_range above the target maximum", "[limits tests]") {
    auto const too_big = safe_unsigned<uint32_t>(uint64_t{std::numeric_limits<uint32_t>::max()} + 1u);
    REQUIRE( ! too_big.has_value());
    REQUIRE(too_big.error() == error::out_of_range);

    auto const byte_overflow = safe_unsigned<uint8_t>(uint32_t{256});
    REQUIRE( ! byte_overflow.has_value());
    REQUIRE(byte_overflow.error() == error::out_of_range);

    auto const byte_max = safe_unsigned<uint8_t>(uint32_t{255});
    REQUIRE(byte_max.has_value());
    REQUIRE(*byte_max == 255u);
}

// End Test Suite
