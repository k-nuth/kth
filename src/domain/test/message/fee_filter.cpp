// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: fee filter tests

TEST_CASE("fee filter  constructor 1  always invalid", "[fee filter]") {
    fee_filter const instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("fee filter  constructor 2  always  equals params", "[fee filter]") {
    uint64_t const value = 6434u;
    fee_filter const instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance.minimum_fee());
}

TEST_CASE("fee filter  constructor 3  always  equals params", "[fee filter]") {
    uint64_t const fee = 6434u;
    fee_filter const value(fee);
    fee_filter const instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(fee == instance.minimum_fee());
    REQUIRE(value == instance);
}

TEST_CASE("fee filter  constructor 4  always  equals params", "[fee filter]") {
    uint64_t const fee = 6434u;
    fee_filter const value(fee);
    fee_filter const instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(fee == instance.minimum_fee());
}

TEST_CASE("fee filter from data insufficient bytes failure", "[fee filter]") {
    data_chunk const raw = {0xab, 0x11};
    byte_reader reader(raw);
    auto const result = fee_filter::from_data(reader, version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("fee filter from data insufficient version failure", "[fee filter]") {
    fee_filter const expected{1};
    auto const data = expected.to_data(fee_filter::version_maximum);
    byte_reader reader(data);
    auto const result = fee_filter::from_data(reader, fee_filter::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("fee filter from data roundtrip  success", "[fee filter]") {
    fee_filter const expected{123};
    auto const data = expected.to_data(fee_filter::version_maximum);
    byte_reader reader(data);
    auto const result_exp = fee_filter::from_data(reader, fee_filter::version_maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);

    auto const size = result.serialized_size(version::level::maximum);
    REQUIRE(data.size() == size);
    REQUIRE(expected.serialized_size(version::level::maximum) == size);
}

TEST_CASE("fee filter  minimum fee  roundtrip  success", "[fee filter]") {
    uint64_t const value = 42134u;
    fee_filter instance;
    REQUIRE(instance.minimum_fee() != value);

    instance.set_minimum_fee(value);
    REQUIRE(value == instance.minimum_fee());
}

TEST_CASE("fee filter  operator assign equals  always  matches equivalent", "[fee filter]") {
    fee_filter value(2453u);
    REQUIRE(value.is_valid());

    fee_filter instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("fee filter  operator boolean equals  duplicates  returns true", "[fee filter]") {
    fee_filter const expected(2453u);
    fee_filter instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("fee filter  operator boolean equals  differs  returns false", "[fee filter]") {
    fee_filter const expected(2453u);
    fee_filter instance;
    REQUIRE( ! (instance == expected));
}

TEST_CASE("fee filter  operator boolean not equals  duplicates  returns false", "[fee filter]") {
    fee_filter const expected(2453u);
    fee_filter instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("fee filter  operator boolean not equals  differs  returns true", "[fee filter]") {
    fee_filter const expected(2453u);
    fee_filter instance;
    REQUIRE(instance != expected);
}

// End Test Suite
