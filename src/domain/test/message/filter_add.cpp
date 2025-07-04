// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: filter add tests

TEST_CASE("filter add  constructor 1  always invalid", "[filter add]") {
    message::filter_add instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("filter add  constructor 2  always  equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_add instance(data);
    REQUIRE(instance.is_valid());
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add  constructor 3  always  equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto dup = data;
    message::filter_add instance(std::move(dup));
    REQUIRE(instance.is_valid());
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add  constructor 4  always  equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    const message::filter_add value(data);
    message::filter_add instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add  constructor 5  always  equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_add value(data);
    message::filter_add instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add from data insufficient bytes  failure", "[filter add]") {
    data_chunk raw = {0xab, 0x11};
    message::filter_add instance;

    byte_reader reader(raw);
    auto result = message::filter_add::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("filter add from data insufficient version  failure", "[filter add]") {
    const message::filter_add expected{
        {0x1F, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
         0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
         0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
         0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}};

    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result = message::filter_add::from_data(reader, message::version::level::minimum - 1);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::version_too_low);
}

TEST_CASE("filter add from data valid input  success", "[filter add]") {
    const message::filter_add expected{
        {0x1F, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
         0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
         0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
         0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}};

    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::filter_add::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::maximum));
    REQUIRE(expected.serialized_size(message::version::level::maximum) ==
                        result.serialized_size(message::version::level::maximum));
}



TEST_CASE("filter add  data accessor 1  always  returns initialized value", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_add instance(value);
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add  data accessor 2  always  returns initialized value", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    const message::filter_add instance(value);
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add  data setter 1  roundtrip  success", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_add instance;
    REQUIRE(value != instance.data());
    instance.set_data(value);
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add  data setter 2  roundtrip  success", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    data_chunk dup = value;
    message::filter_add instance;
    REQUIRE(value != instance.data());
    instance.set_data(std::move(dup));
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add  operator assign equals  always  matches equivalent", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_add value(data);
    REQUIRE(value.is_valid());
    message::filter_add instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add  operator boolean equals  duplicates  returns true", "[filter add]") {
    const message::filter_add expected({0x0f, 0xf0, 0x55, 0xaa});
    message::filter_add instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter add  operator boolean equals  differs  returns false", "[filter add]") {
    const message::filter_add expected({0x0f, 0xf0, 0x55, 0xaa});
    message::filter_add instance;
    REQUIRE(instance != expected);
}

TEST_CASE("filter add  operator boolean not equals  duplicates  returns false", "[filter add]") {
    const message::filter_add expected({0x0f, 0xf0, 0x55, 0xaa});
    message::filter_add instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter add  operator boolean not equals  differs  returns true", "[filter add]") {
    const message::filter_add expected({0x0f, 0xf0, 0x55, 0xaa});
    message::filter_add instance;
    REQUIRE(instance != expected);
}

// End Test Suite
