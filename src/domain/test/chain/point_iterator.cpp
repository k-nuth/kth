// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;

#define SOURCE "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f0100"
static auto const valid_raw_point_iterator_source = to_chunk(base16_literal(SOURCE));

// Start Test Suite: point iterator tests

TEST_CASE("point iterator  operator bool  not at end  returns true", "[point iterator]") {
    point_iterator instance(point{});
    REQUIRE((bool)instance);
}

TEST_CASE("point iterator  operator bool at end  returns false", "[point iterator]") {
    point value;
    point_iterator instance(value, static_cast<unsigned>(value.serialized_size(false)));
    REQUIRE( ! instance);
}

TEST_CASE("point iterator  operator asterisk  initialized point  matches source", "[point iterator]") {
    point point;
    byte_reader reader(valid_raw_point_iterator_source);
    auto result = point::from_data(reader, false);
    REQUIRE(result);
    point = std::move(*result);
    point_iterator instance(point);

    for (size_t i = 0; i < valid_raw_point_iterator_source.size(); i++, instance++) {
       REQUIRE(instance);
       REQUIRE(valid_raw_point_iterator_source[i] == (*instance));
    }

    REQUIRE( ! instance);
    REQUIRE(0u == (*instance));
}

TEST_CASE("point iterator  operator arrow  initialized point  matches source", "[point iterator]") {
    point point;
    byte_reader reader(valid_raw_point_iterator_source);
    auto result = point::from_data(reader, false);
    REQUIRE(result);
    point = std::move(*result);
    point_iterator instance(point);
    REQUIRE(valid_raw_point_iterator_source.size() > 0);

    for (size_t i = 0; i < valid_raw_point_iterator_source.size(); i++, instance++) {
       REQUIRE(instance);
       REQUIRE(valid_raw_point_iterator_source[i] == instance.operator->());
    }

    REQUIRE( ! instance);
    REQUIRE(0u == instance.operator->());
}

TEST_CASE("point iterator  operator plus minus int  roundtrip  success", "[point iterator]") {
    point point;
    uint8_t offset = 5u;
    byte_reader reader(valid_raw_point_iterator_source);
    auto result = point::from_data(reader, false);
    REQUIRE(result);
    point = std::move(*result);

    point_iterator instance(point, offset);
    point_iterator expected(instance);

    auto initial = instance++;
    REQUIRE(instance != expected);
    REQUIRE(initial == expected);

    auto modified = instance--;
    REQUIRE(instance == expected);
    REQUIRE(modified != expected);
}

TEST_CASE("point iterator  operator plus minus  roundtrip  success", "[point iterator]") {
    point point;
    uint8_t offset = 5u;
    byte_reader reader(valid_raw_point_iterator_source);
    auto result = point::from_data(reader, false);
    REQUIRE(result);
    point = std::move(*result);

    point_iterator instance(point, offset);
    point_iterator expected(instance);

    ++instance;
    REQUIRE(instance != expected);

    --instance;
    REQUIRE(instance == expected);
}

TEST_CASE("point iterator  copy assigment", "[point iterator]") {
    point point;
    uint8_t offset = 5u;
    byte_reader reader(valid_raw_point_iterator_source);
    auto result = point::from_data(reader, false);
    REQUIRE(result);
    point = std::move(*result);

    point_iterator instance(point, offset);
    point_iterator expected(instance);

    instance = expected;
}

// End Test Suite
