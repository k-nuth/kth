// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>

using namespace kth;
using namespace kd;

// Start Test Suite: limits tests

// cast_add
//-----------------------------------------------------------------------------

TEST_CASE("limits cast add  uint32 to int64 0  returns 0", "[limits]") {
    static int64_t const expected = 0;
    REQUIRE(cast_add<int64_t>(0u, 0u) == expected);
}

TEST_CASE("limits cast add  uint32 to int64 maximum plus 0  returns maximum", "[limits]") {
    static int64_t const expected = max_uint32;
    REQUIRE(cast_add<int64_t>(max_uint32, uint32_t{0}) == expected);
}

TEST_CASE("limits cast add  uint32 to int64 maximum plus maximum returns twice maximum", "[limits]") {
    static int64_t const expected = 2 * int64_t(max_uint32);
    REQUIRE(cast_add<int64_t>(max_uint32, max_uint32) == expected);
}

// cast_subtract
//-----------------------------------------------------------------------------

TEST_CASE("limits cast subtract  uint32 to int64 0  returns 0", "[limits]") {
    static int64_t const expected = 0;
    REQUIRE(cast_subtract<int64_t>(0u, 0u) == expected);
}

TEST_CASE("limits cast subtract  uint32 to int64 0 minus maximum returns negtive maximum", "[limits]") {
    static int64_t const expected = -1 * int64_t(max_uint32);
    REQUIRE(cast_subtract<int64_t>(uint32_t{0}, max_uint32) == expected);
}

TEST_CASE("limits cast subtract  uint32 to int64 maximum minus maximum returns 0", "[limits]") {
    static int64_t const expected = 0;
    REQUIRE(cast_subtract<int64_t>(max_uint32, max_uint32) == expected);
}

static size_t const minimum = 0;
static size_t const maximum = max_size_t;
static size_t const half = maximum / 2;

// ceiling_add
//-----------------------------------------------------------------------------

TEST_CASE("limits ceiling add size_t minimum plus minimum minimum", "[limits]") {
    REQUIRE(ceiling_add(minimum, minimum) == minimum);
}

TEST_CASE("limits ceiling add size_t maximum plus maximum maximum", "[limits]") {
    REQUIRE(ceiling_add(maximum, maximum) == maximum);
}

TEST_CASE("limits ceiling add size_t minimum plus maximum maximum", "[limits]") {
    REQUIRE(ceiling_add(minimum, maximum) == maximum);
}

TEST_CASE("limits ceiling add size_t maximum plus minimum maximum", "[limits]") {
    REQUIRE(ceiling_add(maximum, minimum) == maximum);
}

TEST_CASE("limits ceiling add size_t half plus maximum maximum", "[limits]") {
    REQUIRE(ceiling_add(half, maximum) == maximum);
}

// floor_subtract
//-----------------------------------------------------------------------------

TEST_CASE("limits floor subtract size_t minimum minus minimum minimum", "[limits]") {
    REQUIRE(floor_subtract(minimum, minimum) == minimum);
}

TEST_CASE("limits floor subtract size_t maximum minus maximum minimum", "[limits]") {
    REQUIRE(floor_subtract(maximum, maximum) == minimum);
}

TEST_CASE("limits floor subtract size_t maximum minus minimum maximum", "[limits]") {
    REQUIRE(floor_subtract(maximum, minimum) == maximum);
}

TEST_CASE("limits floor subtract size_t minimum minus maximum minimum", "[limits]") {
    REQUIRE(floor_subtract(minimum, maximum) == minimum);
}

TEST_CASE("limits floor subtract size_t half minus maximum minimum", "[limits]") {
    REQUIRE(floor_subtract(half, maximum) == minimum);
}

static uint32_t const min_uint32 = 0;
static uint32_t const half_uint32 = max_uint32 / 2;

// ceiling_add32
//-----------------------------------------------------------------------------

TEST_CASE("limits ceiling add min uint32 plus minimum min uint32", "[limits]") {
    REQUIRE(ceiling_add(min_uint32, min_uint32) == min_uint32);
}

TEST_CASE("limits ceiling add max uint32 plus max uint32 max uint32", "[limits]") {
    REQUIRE(ceiling_add(max_uint32, max_uint32) == max_uint32);
}

TEST_CASE("limits ceiling add min uint32 plus max uint32 max uint32", "[limits]") {
    REQUIRE(ceiling_add(min_uint32, max_uint32) == max_uint32);
}

TEST_CASE("limits ceiling add max uint32 plus min uint32 max uint32", "[limits]") {
    REQUIRE(ceiling_add(max_uint32, min_uint32) == max_uint32);
}

TEST_CASE("limits ceiling add  half uint32 plus max uint32 max uint32", "[limits]") {
    REQUIRE(ceiling_add(half_uint32, max_uint32) == max_uint32);
}

// floor_subtract32
//-----------------------------------------------------------------------------

TEST_CASE("limits floor subtract min uint32 minus min uint32 min uint32", "[limits]") {
    REQUIRE(floor_subtract(min_uint32, min_uint32) == min_uint32);
}

TEST_CASE("limits floor subtract max uint32 minus max uint32 min uint32", "[limits]") {
    REQUIRE(floor_subtract(max_uint32, max_uint32) == min_uint32);
}

TEST_CASE("limits floor subtract max uint32 minus min uint32 max uint32", "[limits]") {
    REQUIRE(floor_subtract(max_uint32, min_uint32) == max_uint32);
}

TEST_CASE("limits floor subtract min uint32 minus max uint32 min uint32", "[limits]") {
    REQUIRE(floor_subtract(min_uint32, max_uint32) == min_uint32);
}

TEST_CASE("limits floor subtract  half uint32 minus max uint32 min uint32", "[limits]") {
    REQUIRE(floor_subtract(half_uint32, max_uint32) == min_uint32);
}

static uint64_t const min_uint64 = 0;
static uint64_t const half_uint64 = max_uint64 / 2;

// ceiling_add64
//-----------------------------------------------------------------------------

TEST_CASE("limits ceiling add min uint64 plus min uint64 min uint64", "[limits]") {
    REQUIRE(ceiling_add(min_uint64, min_uint64) == min_uint64);
}

TEST_CASE("limits ceiling add max uint64 plus max uint64 max uint64", "[limits]") {
    REQUIRE(ceiling_add(max_uint64, max_uint64) == max_uint64);
}

TEST_CASE("limits ceiling add min uint64 plus max uint64 max uint64", "[limits]") {
    REQUIRE(ceiling_add(min_uint64, max_uint64) == max_uint64);
}

TEST_CASE("limits ceiling add max uint64 plus min uint64 max uint64", "[limits]") {
    REQUIRE(ceiling_add(max_uint64, min_uint64) == max_uint64);
}

TEST_CASE("limits ceiling add  half uint64 plus max uint64 max uint64", "[limits]") {
    REQUIRE(ceiling_add(half_uint64, max_uint64) == max_uint64);
}

// floor_subtract64
//-----------------------------------------------------------------------------

TEST_CASE("limits floor subtract min uint64 minus min uint64 min uint64", "[limits]") {
    REQUIRE(floor_subtract(min_uint64, min_uint64) == min_uint64);
}

TEST_CASE("limits floor subtract max uint64 minus max uint64 min uint64", "[limits]") {
    REQUIRE(floor_subtract(max_uint64, max_uint64) == min_uint64);
}

TEST_CASE("limits floor subtract max uint64 minus min uint64 max uint64", "[limits]") {
    REQUIRE(floor_subtract(max_uint64, min_uint64) == max_uint64);
}

TEST_CASE("limits floor subtract min uint64 minus max uint64 min uint64", "[limits]") {
    REQUIRE(floor_subtract(min_uint64, max_uint64) == min_uint64);
}

TEST_CASE("limits floor subtract  half uint64 minus max uint64 min uint64", "[limits]") {
    REQUIRE(floor_subtract(half_uint64, max_uint64) == min_uint64);
}

// safe_add
//-----------------------------------------------------------------------------

TEST_CASE("limits safe add size_t minimum plus minimum minimum", "[limits]") {
    REQUIRE(*safe_add(minimum, minimum) == minimum);
}

TEST_CASE("limits safe add size_t maximum plus maximum returns overflow", "[limits]") {
    auto const expr = safe_add(maximum, maximum);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::overflow);
}

TEST_CASE("limits safe add size_t minimum plus maximum maximum", "[limits]") {
    REQUIRE(*safe_add(minimum, maximum) == maximum);
}

TEST_CASE("limits safe add size_t maximum plus minimum maximum", "[limits]") {
    REQUIRE(*safe_add(maximum, minimum) == maximum);
}

TEST_CASE("limits safe add size_t half plus maximum returns overflow", "[limits]") {
    auto const expr = safe_add(half, maximum);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::overflow);
}

// safe_subtract
//-----------------------------------------------------------------------------

TEST_CASE("limits safe subtract size_t minimum minus minimum minimum", "[limits]") {
    REQUIRE(safe_subtract(minimum, minimum) == minimum);
}

TEST_CASE("limits safe subtract size_t maximum minus maximum minimum", "[limits]") {
    REQUIRE(safe_subtract(maximum, maximum) == minimum);
}

TEST_CASE("limits safe subtract size_t maximum minus minimum maximum", "[limits]") {
    REQUIRE(safe_subtract(maximum, minimum) == maximum);
}

TEST_CASE("limits safe subtract size_t minimum minus maximum returns underflow", "[limits]") {
    auto const expr = safe_subtract(minimum, maximum);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::underflow);
}

TEST_CASE("limits safe subtract size_t half minus maximum returns underflow", "[limits]") {
    auto const expr = safe_subtract(half, maximum);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::underflow);
}

// // safe_increment
// //-----------------------------------------------------------------------------

// TEST_CASE("limits safe increment size_t minimum expected", "[limits]") {
//     auto value = minimum;
//     static auto const expected = minimum + 1u;
//     safe_increment(value);
//     REQUIRE(value == expected);
// }

// TEST_CASE("limits safe increment size_t half  expected", "[limits]") {
//     auto value = half;
//     static auto const expected = half + 1u;
//     safe_increment(value);
//     REQUIRE(value == expected);
// }

// TEST_CASE("limits safe increment size_t maximum throws overflow", "[limits]") {
//     auto value = maximum;
//     REQUIRE_THROWS_AS(safe_increment(value), std::overflow_error);
// }

// // safe_decrement
// //-----------------------------------------------------------------------------

// TEST_CASE("limits safe decrement size_t maximum expected", "[limits]") {
//     auto value = maximum;
//     static auto const expected = maximum - 1u;
//     safe_decrement(value);
//     REQUIRE(value == expected);
// }

// TEST_CASE("limits safe decrement size_t half  expected", "[limits]") {
//     auto value = half;
//     static auto const expected = half - 1u;
//     safe_decrement(value);
//     REQUIRE(value == expected);
// }

// TEST_CASE("limits safe decrement size_t minimum throws underflow", "[limits]") {
//     auto value = minimum;
//     REQUIRE_THROWS_AS(safe_decrement(value), std::underflow_error);
// }

// safe_signed
//-----------------------------------------------------------------------------

TEST_CASE("limits safe signed min int32 to int32 min int32", "[limits]") {
    REQUIRE(safe_signed<int32_t>(min_int32) == min_int32);
}

TEST_CASE("limits safe signed max int32 to int32 max int32", "[limits]") {
    REQUIRE(safe_signed<int32_t>(max_int32) == max_int32);
}

TEST_CASE("limits safe signed min int64 to int32  returns range", "[limits]") {
    auto const expr = safe_signed<int32_t>(min_int64);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::out_of_range);
}

// safe_unsigned
//-----------------------------------------------------------------------------

TEST_CASE("limits safe unsigned min uint32 to uint32 min uint32", "[limits]") {
    REQUIRE(*safe_unsigned<uint32_t>(min_uint32) == min_uint32);
}

TEST_CASE("limits safe unsigned max uint32 to uint32 max uint32", "[limits]") {
    REQUIRE(*safe_unsigned<uint32_t>(max_uint32) == max_uint32);
}

TEST_CASE("limits safe unsigned max uint64 to uint32 returns range", "[limits]") {
    auto const expr = safe_unsigned<uint32_t>(max_uint64);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::out_of_range);
}

// safe_to_signed
//-----------------------------------------------------------------------------

TEST_CASE("limits safe to signed min uint32 to int32 min uint32", "[limits]") {
    REQUIRE(safe_to_signed<int32_t>(min_uint32) == (int32_t)min_uint32);
}

TEST_CASE("limits safe to signed max uint32 to int32 returns range", "[limits]") {
    auto const expr = safe_to_signed<int32_t>(max_uint32);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::out_of_range);
}

TEST_CASE("limits safe to signed min uint64 to int32 min uint64", "[limits]") {
    REQUIRE(safe_to_signed<int32_t>(min_uint64) == (int32_t)min_uint64);
}

// safe_to_unsigned
//-----------------------------------------------------------------------------

TEST_CASE("limits safe to unsigned min int32 to uint32 returns range", "[limits]") {
    auto const expr = safe_to_unsigned<uint32_t>(min_int32);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::out_of_range);
}

TEST_CASE("limits safe to unsigned max int32 to uint32 max int32", "[limits]") {
    REQUIRE(safe_to_unsigned<uint32_t>(max_int32) == (uint32_t)max_int32);
}

TEST_CASE("limits safe to unsigned min int64 to uint32 returns range", "[limits]") {
    auto const expr = safe_to_unsigned<uint32_t>(min_int64);
    REQUIRE( ! expr.has_value());
    REQUIRE(expr.error() == error::out_of_range);
}

// range_constrain
//-----------------------------------------------------------------------------

TEST_CASE("limits range constrain over max", "[limits]") {
   size_t const expected = 10;
    auto const result = range_constrain(size_t(42), size_t(1), expected);
    REQUIRE(result == expected);
}

TEST_CASE("limits range constrain under min", "[limits]") {
   size_t const expected = 50;
    auto const result = range_constrain(size_t(42), expected, size_t(100));
    REQUIRE(result == expected);
}

TEST_CASE("limits range constrain internal unchanged", "[limits]") {
   size_t const expected = 42;
    auto const result = range_constrain(expected, size_t(10), size_t(100));
    REQUIRE(result == expected);
}

// End Test Suite
