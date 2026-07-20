// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;

// Start Test Suite: history tests

// These tests exist mainly to ODR-use the comparison operators. Both
// `history_compact` and `history` carry an anonymous union, so a defaulted
// `<=>` would be *deleted* (per [class.compare.default]) and only fail at
// the first use — which no test previously exercised. Comparing values here
// keeps the hand-written operators honest.

static auto const hash_a = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
static auto const hash_b = "00000000000000000000000000000000000000000000000000000000000000ff"_hash;

TEST_CASE("history_compact equality and ordering", "[history]") {
    history_compact const a{point_kind::output, {hash_a, 1}, 100, {.value = 5000}};
    history_compact const b = a;
    history_compact const c{point_kind::output, {hash_a, 1}, 100, {.value = 6000}};

    REQUIRE(a == b);
    REQUIRE( ! (a != b));
    REQUIRE(a != c);
    REQUIRE(a < c);        // differs only by the union value (5000 < 6000)
    REQUIRE(c > a);
    REQUIRE(a <= b);
    REQUIRE(a >= b);
}

TEST_CASE("history_compact ordering walks fields before the union", "[history]") {
    history_compact const lo{point_kind::output, {hash_a, 1}, 100, {.value = 9999}};
    history_compact const hi{point_kind::output, {hash_a, 1}, 101, {.value = 0}};

    // Higher `height` sorts after regardless of the (smaller) union value.
    REQUIRE(lo < hi);
}

TEST_CASE("history equality and ordering", "[history]") {
    history const a{output_point{hash_a, 0}, 10, 5000, input_point{hash_b, 1}, {.spend_height = 20}};
    history const b = a;
    history const c{output_point{hash_a, 0}, 10, 5000, input_point{hash_b, 1}, {.spend_height = 21}};

    REQUIRE(a == b);
    REQUIRE( ! (a != b));
    REQUIRE(a != c);
    REQUIRE(a < c);        // differs only by the union spend_height (20 < 21)
    REQUIRE(c > a);
    REQUIRE(a <= b);
    REQUIRE(a >= b);
}

// End Test Suite
