// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

auto const hash1 = hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
auto const valid_raw_output_point = to_chunk("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f00000015"_base16);

// Start Test Suite: output point tests

TEST_CASE("output point  constructor 1  always  returns default initialized", "[output point]") {
    const chain::point instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("output point  constructor 2  valid input  returns input initialized", "[output point]") {
    static uint32_t const index = 1234u;
    const chain::point value(hash1, index);
    chain::output_point instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(hash1 == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("output point  constructor 3  valid input  returns input initialized", "[output point]") {
    static uint32_t const index = 1234u;
    chain::point value(hash1, index);
    chain::output_point instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(hash1 == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("output point  constructor 4  valid input  returns input initialized", "[output point]") {
    static uint32_t const index = 1234u;
    chain::output_point instance(hash1, index);
    REQUIRE(instance.is_valid());
    REQUIRE(hash1 == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("output point  constructor 5  valid input  returns input initialized", "[output point]") {
    static uint32_t const index = 1234u;
    auto dup_hash = hash1;
    chain::output_point instance(std::move(dup_hash), index);
    REQUIRE(instance.is_valid());
    REQUIRE(hash1 == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("output point  constructor 6  valid input  returns input initialized", "[output point]") {
    const chain::output_point expected(hash1, 524342u);
    chain::output_point instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("output point  constructor 7  valid input  returns input initialized", "[output point]") {
    chain::output_point expected(hash1, 524342u);
    chain::output_point instance(std::move(expected));
    REQUIRE(instance.is_valid());
}

TEST_CASE("output point  begin end  initialized  begin not equal end", "[output point]") {
    static const chain::output_point instance{null_hash, 0};
    REQUIRE(instance.begin() != instance.end());
}

TEST_CASE("output point from data insufficient bytes  failure", "[output point]") {
    static data_chunk const data(10);
    chain::output_point instance;
    byte_reader reader(data);
    auto result = chain::output_point::from_data(reader);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("output point from data roundtrip  success", "[output point]") {
    static uint32_t const index = 53213u;
    static hash_digest const hash{
        {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
         0x01, 0x01, 0xab, 0x11, 0x11, 0xcd, 0x11, 0x11,
         0x01, 0x10, 0x11, 0xab, 0x11, 0x11, 0xcd, 0x11,
         0x01, 0x11, 0x11, 0x11, 0xab, 0x11, 0x11, 0xcd}};

    chain::output_point initial{hash, index};

    REQUIRE(initial.is_valid());
    REQUIRE(hash == initial.hash());
    REQUIRE(index == initial.index());

    data_chunk output = initial.to_data();
    byte_reader reader(output);
    auto result_exp = chain::output_point::from_data(reader);
    REQUIRE(result_exp);
    auto const point = std::move(*result_exp);
    REQUIRE(point.is_valid());
    REQUIRE(point == initial);
}

TEST_CASE("output point from data roundtrip  success 2", "[output point]") {
    static auto const data = to_chunk("46682488f0a721124a3905a1bb72445bf13493e2cd46c5c0c8db1c15afa0d58e00000000"_base16);
    REQUIRE(data == (data_chunk{
                              0x46, 0x68, 0x24, 0x88, 0xf0, 0xa7, 0x21, 0x12, 0x4a, 0x39, 0x05, 0xa1,
                              0xbb, 0x72, 0x44, 0x5b, 0xf1, 0x34, 0x93, 0xe2, 0xcd, 0x46, 0xc5, 0xc0,
                              0xc8, 0xdb, 0x1c, 0x15, 0xaf, 0xa0, 0xd5, 0x8e, 0x00, 0x00, 0x00, 0x00}));

    byte_reader reader(data);
    auto result_exp = chain::output_point::from_data(reader);
    REQUIRE(result_exp);
    auto const point = std::move(*result_exp);

    REQUIRE(point.is_valid());
    REQUIRE(encode_hash(point.hash()) == "8ed5a0af151cdbc8c0c546cde29334f15b4472bba105394a1221a7f088246846");
    REQUIRE(point.index() == 0);

    data_chunk output = point.to_data();
    REQUIRE(output == data);
}

TEST_CASE("output point  factory from data 2  roundtrip  success", "[output point]") {
    static auto const data = to_chunk("46682488f0a721124a3905a1bb72445bf13493e2cd46c5c0c8db1c15afa0d58e00000000"_base16);
    REQUIRE(data == (data_chunk{
                              0x46, 0x68, 0x24, 0x88, 0xf0, 0xa7, 0x21, 0x12, 0x4a, 0x39, 0x05, 0xa1,
                              0xbb, 0x72, 0x44, 0x5b, 0xf1, 0x34, 0x93, 0xe2, 0xcd, 0x46, 0xc5, 0xc0,
                              0xc8, 0xdb, 0x1c, 0x15, 0xaf, 0xa0, 0xd5, 0x8e, 0x00, 0x00, 0x00, 0x00}));

    byte_reader reader(data);
    auto result_exp = chain::output_point::from_data(reader);
    REQUIRE(result_exp);
    auto const point = std::move(*result_exp);

    REQUIRE(point.is_valid());
    REQUIRE(encode_hash(point.hash()) == "8ed5a0af151cdbc8c0c546cde29334f15b4472bba105394a1221a7f088246846");
    REQUIRE(point.index() == 0);

    data_chunk output = point.to_data();
    REQUIRE(output == data);
}

TEST_CASE("output point  factory from data 3  roundtrip  success", "[output point]") {
    static auto const data = to_chunk("46682488f0a721124a3905a1bb72445bf13493e2cd46c5c0c8db1c15afa0d58e00000000"_base16);
    REQUIRE(data == (data_chunk{
                              0x46, 0x68, 0x24, 0x88, 0xf0, 0xa7, 0x21, 0x12, 0x4a, 0x39, 0x05, 0xa1,
                              0xbb, 0x72, 0x44, 0x5b, 0xf1, 0x34, 0x93, 0xe2, 0xcd, 0x46, 0xc5, 0xc0,
                              0xc8, 0xdb, 0x1c, 0x15, 0xaf, 0xa0, 0xd5, 0x8e, 0x00, 0x00, 0x00, 0x00}));

    byte_reader reader(data);
    auto result_exp = chain::output_point::from_data(reader);
    REQUIRE(result_exp);
    auto point = std::move(*result_exp);

    REQUIRE(point.is_valid());
    REQUIRE(encode_hash(point.hash()) == "8ed5a0af151cdbc8c0c546cde29334f15b4472bba105394a1221a7f088246846");
    REQUIRE(point.index() == 0);

    data_chunk output = point.to_data();
    REQUIRE(output == data);
}

TEST_CASE("output point  is mature  mature coinbase prevout  returns true", "[output point]") {
    size_t target_height = 162u;
    chain::output_point instance(hash1, 42);
    instance.validation.height = 50u;
    instance.validation.coinbase = true;
    REQUIRE( ! instance.is_null());
    REQUIRE(instance.is_mature(target_height));
}

TEST_CASE("output point  is mature  immature coinbase prevout  returns false", "[output point]") {
    size_t target_height = 162u;
    chain::output_point instance(hash1, 42);
    instance.validation.height = 100u;
    instance.validation.coinbase = true;
    REQUIRE( ! instance.is_null());
    REQUIRE( ! instance.is_mature(target_height));
}

TEST_CASE("output point  is mature  immature coinbase prevout null input  returns true", "[output point]") {
    size_t target_height = 162u;
    chain::output_point instance(null_hash, chain::point::null_index);
    instance.validation.height = 100u;
    instance.validation.coinbase = true;
    REQUIRE(instance.is_null());
    REQUIRE(instance.is_mature(target_height));
}

TEST_CASE("output point  is mature  mature non coinbase prevout  returns true", "[output point]") {
    size_t target_height = 162u;
    chain::output_point instance(hash1, 42);
    instance.validation.height = 50u;
    instance.validation.coinbase = false;
    REQUIRE( ! instance.is_null());
    REQUIRE(instance.is_mature(target_height));
}

TEST_CASE("output point  is mature  immature non coinbase prevout  returns true", "[output point]") {
    size_t target_height = 162u;
    chain::output_point instance(hash1, 42);
    instance.validation.height = 100u;
    instance.validation.coinbase = false;
    REQUIRE( ! instance.is_null());
    REQUIRE(instance.is_mature(target_height));
}

TEST_CASE("output point  operator assign equals 1  always  matches equivalent", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::output_point instance;
    chain::output_point value;
    reader.reset();
    result = chain::output_point::from_data(reader);
    REQUIRE(result);
    value = std::move(*result);
    instance = std::move(value);
    REQUIRE(instance == expected);
}

TEST_CASE("output point  operator assign equals 2  always  matches equivalent", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::output_point instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("output point  operator assign equals 3  always  matches equivalent", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    reader.reset();
    result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto instance = std::move(*result);
    REQUIRE(instance == expected);
}

TEST_CASE("output point  operator assign equals 4  always  matches equivalent", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::output_point instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("output point  operator boolean equals 1  duplicates  returns true", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    auto const alpha = std::move(*result);
    reader.reset();
    auto result2 = chain::output_point::from_data(reader);
    REQUIRE(result2);
    auto const beta = std::move(*result2);
    REQUIRE(alpha == beta);
}

TEST_CASE("output point  operator boolean equals 1  differs  returns false", "[output point]") {
    chain::output_point alpha;
    chain::output_point beta;
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("output point  operator boolean equals 2  duplicates  returns true", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const alpha = std::move(*result);
    reader.reset();
    result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("output point  operator boolean equals 2  differs  returns false", "[output point]") {
    chain::output_point alpha;
    chain::point beta;
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("output point  operator boolean not equals 1  duplicates  returns false", "[output point]") {
    chain::output_point alpha;
    chain::output_point beta;
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::output_point::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("output point  operator boolean not equals 1  differs  returns true", "[output point]") {
    chain::output_point alpha;
    chain::output_point beta;
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("output point  operator boolean not equals 2  duplicates  returns false", "[output point]") {
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const alpha = std::move(*result);
    reader.reset();
    result = chain::output_point::from_data(reader);
    REQUIRE(result);
    auto const beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("output point  operator boolean not equals 2  differs  returns true", "[output point]") {
    chain::output_point alpha;
    chain::point beta;
    byte_reader reader(valid_raw_output_point);
    auto result = chain::output_point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

// End Test Suite
