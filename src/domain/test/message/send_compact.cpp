// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: send compact tests

TEST_CASE("send compact  constructor 1  always invalid", "[send compact]") {
    message::send_compact instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("send compact  constructor 2  always  equals params", "[send compact]") {
    bool mode = true;
    uint64_t version = 1245436u;
    message::send_compact instance(mode, version);
    REQUIRE(mode == instance.high_bandwidth_mode());
    REQUIRE(version == instance.version());
}

TEST_CASE("send compact  constructor 3  always  equals params", "[send compact]") {
    const message::send_compact expected(true, 1245436u);
    message::send_compact instance(expected);
    REQUIRE(expected == instance);
}

TEST_CASE("send compact  constructor 4  always  equals params", "[send compact]") {
    bool mode = true;
    uint64_t version = 1245436u;
    message::send_compact expected(mode, version);
    REQUIRE(expected.is_valid());

    message::send_compact instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(mode == instance.high_bandwidth_mode());
    REQUIRE(version == instance.version());
}

TEST_CASE("send compact from data valid input  success", "[send compact]") {
    const message::send_compact expected{true, 164};
    auto const data = expected.to_data(message::send_compact::version_minimum);
    byte_reader reader(data);
    auto const result_exp = message::send_compact::from_data(reader, message::send_compact::version_minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(message::send_compact::satoshi_fixed_size(message::send_compact::version_minimum) == data.size());
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}



TEST_CASE("send compact  from data 1  invalid mode byte  failure", "[send compact]") {
    data_chunk raw_data{0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    message::send_compact msg;
    byte_reader reader(raw_data);
    auto exp_result = message::send_compact::from_data(reader, message::send_compact::version_minimum);
    bool result = static_cast<bool>(exp_result);
    if (result) msg = std::move(*exp_result);
    REQUIRE( ! result);
}

TEST_CASE("send compact  from data 1  insufficient version  failure", "[send compact]") {
    const message::send_compact expected{true, 257};
    data_chunk raw_data = expected.to_data(message::send_compact::version_minimum);
    message::send_compact msg;
    byte_reader reader(raw_data);
    auto exp_result = message::send_compact::from_data(reader, message::send_compact::version_minimum - 1);
    bool result = static_cast<bool>(exp_result);
    if (result) msg = std::move(*exp_result);
    REQUIRE( ! result);
}

TEST_CASE("send compact  high bandwidth mode accessor  always  returns initialized value", "[send compact]") {
    bool const expected = true;
    const message::send_compact instance(expected, 210u);
    REQUIRE(expected == instance.high_bandwidth_mode());
}

TEST_CASE("send compact  high bandwidth mode setter  roundtrip  success", "[send compact]") {
    bool const expected = true;
    message::send_compact instance;
    REQUIRE(expected != instance.high_bandwidth_mode());
    instance.set_high_bandwidth_mode(expected);
    REQUIRE(expected == instance.high_bandwidth_mode());
}

TEST_CASE("send compact  version accessor  always  returns initialized value", "[send compact]") {
    uint64_t const expected = 6548u;
    const message::send_compact instance(false, expected);
    REQUIRE(expected == instance.version());
}

TEST_CASE("send compact  version setter  roundtrip  success", "[send compact]") {
    uint64_t const expected = 6548u;
    message::send_compact instance;
    REQUIRE(expected != instance.version());
    instance.set_version(expected);
    REQUIRE(expected == instance.version());
}

TEST_CASE("send compact  operator assign equals  always  matches equivalent", "[send compact]") {
    bool mode = false;
    uint64_t version = 210u;
    message::send_compact value(mode, version);
    REQUIRE(value.is_valid());

    message::send_compact instance;
    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(mode == instance.high_bandwidth_mode());
    REQUIRE(version == instance.version());
}

TEST_CASE("send compact  operator boolean equals  duplicates  returns true", "[send compact]") {
    const message::send_compact expected(false, 15234u);
    message::send_compact instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("send compact  operator boolean equals  differs  returns false", "[send compact]") {
    const message::send_compact expected(true, 979797u);
    message::send_compact instance;
    REQUIRE(instance != expected);
}

TEST_CASE("send compact  operator boolean not equals  duplicates  returns false", "[send compact]") {
    const message::send_compact expected(true, 734678u);
    message::send_compact instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("send compact  operator boolean not equals  differs  returns true", "[send compact]") {
    const message::send_compact expected(false, 5357534u);
    message::send_compact instance;
    REQUIRE(instance != expected);
}

// End Test Suite
