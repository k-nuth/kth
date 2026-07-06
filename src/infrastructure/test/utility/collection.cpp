// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: collection tests

typedef std::vector<uint8_t> collection;

// distinct

TEST_CASE("collection distinct empty same", "[collection tests]") {
    collection parameter;
    auto const& result = distinct(parameter);
    REQUIRE(parameter.empty());
    REQUIRE(&result == &parameter);
}

TEST_CASE("collection distinct single match", "[collection tests]") {
    uint8_t const expected = 42;
    collection set{ expected };
    auto const& result = distinct(set);
    REQUIRE(result.size() == 1u);
    REQUIRE(result[0] == expected);
}

TEST_CASE("collection distinct distinct sorted sorted", "[collection tests]") {
    collection set{ 0, 2, 4, 6, 8 };
    auto const& result = distinct(set);
    REQUIRE(result.size() == 5u);
    REQUIRE(result[0] == 0u);
    REQUIRE(result[1] == 2u);
    REQUIRE(result[2] == 4u);
    REQUIRE(result[3] == 6u);
    REQUIRE(result[4] == 8u);
}

TEST_CASE("collection distinct distinct unsorted sorted", "[collection tests]") {
    collection set{ 2, 0, 8, 6, 4 };
    auto const& result = distinct(set);
    REQUIRE(result.size() == 5u);
    REQUIRE(result[0] == 0u);
    REQUIRE(result[1] == 2u);
    REQUIRE(result[2] == 4u);
    REQUIRE(result[3] == 6u);
    REQUIRE(result[4] == 8u);
}

TEST_CASE("collection distinct distinct unsorted duplicates sorted distinct", "[collection tests]") {
    collection set{ 2, 0, 0, 8, 6, 4 };
    auto const& result = distinct(set);
    REQUIRE(result.size() == 5u);
    REQUIRE(result[0] == 0u);
    REQUIRE(result[1] == 2u);
    REQUIRE(result[2] == 4u);
    REQUIRE(result[3] == 6u);
    REQUIRE(result[4] == 8u);
}

// move_append

TEST_CASE("collection move append both empty both empty", "[collection tests]") {
    collection source;
    collection target;
    move_append(target, source);
    REQUIRE(source.size() == 0u);
    REQUIRE(target.size() == 0u);
}

TEST_CASE("collection move append source empty both unchanged", "[collection tests]") {
    collection source;
    collection target{ 0, 2, 4, 6, 8 };
    auto const expected = target.size();
    move_append(target, source);
    REQUIRE(source.size() == 0u);
    REQUIRE(target.size() == expected);
    REQUIRE(target[0] == 0u);
    REQUIRE(target[1] == 2u);
    REQUIRE(target[2] == 4u);
    REQUIRE(target[3] == 6u);
    REQUIRE(target[4] == 8u);
}

TEST_CASE("collection move append target empty swapped values", "[collection tests]") {
    collection source{ 0, 2, 4, 6, 8 };
    collection target;
    auto const expected = source.size();
    move_append(target, source);
    REQUIRE(source.size() == 0u);
    REQUIRE(target.size() == expected);
    REQUIRE(target[0] == 0u);
    REQUIRE(target[1] == 2u);
    REQUIRE(target[2] == 4u);
    REQUIRE(target[3] == 6u);
    REQUIRE(target[4] == 8u);
}

TEST_CASE("collection move append neither empty moved in order", "[collection tests]") {
    collection source{ 10, 12, 14, 16, 18 };
    collection target{ 0, 2, 4, 6, 8 };
    auto const expected = source.size() + source.size();
    move_append(target, source);
    REQUIRE(source.size() == 0u);
    REQUIRE(target.size() == expected);
    REQUIRE(target[0] == 0u);
    REQUIRE(target[1] == 2u);
    REQUIRE(target[2] == 4u);
    REQUIRE(target[3] == 6u);
    REQUIRE(target[4] == 8u);
    REQUIRE(target[5] == 10u);
    REQUIRE(target[6] == 12u);
    REQUIRE(target[7] == 14u);
    REQUIRE(target[8] == 16u);
    REQUIRE(target[9] == 18u);
}

TEST_CASE("collection pop single empty and returns expected", "[collection tests]") {
    uint8_t const expected = 42u;
    collection stack{ expected };
    auto const value = pop(stack);
    REQUIRE(stack.empty());
    REQUIRE(value == expected);
}

TEST_CASE("collection pop multiple popped and returns expected", "[collection tests]") {
    uint8_t const expected = 42u;
    collection stack{ 0, 1, 2, 3, expected };
    auto const value = pop(stack);
    REQUIRE(stack.size() == 4u);
    REQUIRE(stack[0] == 0u);
    REQUIRE(stack[1] == 1u);
    REQUIRE(stack[2] == 2u);
    REQUIRE(stack[3] == 3u);
    REQUIRE(value == expected);
}

// End Test Suite
