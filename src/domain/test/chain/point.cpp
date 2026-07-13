// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

auto const valid_raw_point = to_chunk("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f00000015"_base16);

// Start Test Suite: point tests

TEST_CASE("point constructor 2 valid input returns input initialized", "[point]") {
    auto const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
    uint32_t index = 1234u;

    chain::point instance(hash, index);
    REQUIRE(hash == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("point constructor 3 valid input returns input initialized", "[point]") {
    auto const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
    uint32_t index = 1234u;

    // This must be non-const.
    auto dup_hash = hash;

    chain::point instance(std::move(dup_hash), index);
    REQUIRE(hash == instance.hash());
    REQUIRE(index == instance.index());
}

TEST_CASE("point constructor 4 valid input returns input initialized", "[point]") {
    const chain::point expected{
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        524342u};

    chain::point instance(expected);
    REQUIRE(expected == instance);
}

TEST_CASE("point constructor 5 valid input returns input initialized", "[point]") {
    // This must be non-const.
    chain::point expected{
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        524342u};

    chain::point instance(std::move(expected));
}

TEST_CASE("point begin end initialized begin not equal end", "[point]") {
    chain::point instance{null_hash, 0};

    REQUIRE(instance.begin() != instance.end());
}

TEST_CASE("point from data insufficient bytes failure", "[point]") {
    data_chunk data(10);
    chain::point instance{null_hash, 0u};

    byte_reader reader(data);
    auto result = chain::point::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("point from data roundtrip success", "[point]") {
    uint32_t index = 53213;
    hash_digest const hash{
        {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
         0x01, 0x01, 0xab, 0x11, 0x11, 0xcd, 0x11, 0x11,
         0x01, 0x10, 0x11, 0xab, 0x11, 0x11, 0xcd, 0x11,
         0x01, 0x11, 0x11, 0x11, 0xab, 0x11, 0x11, 0xcd}};

    chain::point initial{hash, index};
    REQUIRE(hash == initial.hash());
    REQUIRE(index == initial.index());

    data_chunk output = kth::to_data_chunk(initial, true);
    byte_reader reader(output);
    auto result_exp = chain::point::from_data(reader);
    REQUIRE(result_exp);
    auto const point = std::move(*result_exp);
    REQUIRE(point == initial);
}

TEST_CASE("point from data roundtrip success 2", "[point]") {
    auto const raw = to_chunk("46682488f0a721124a3905a1bb72445bf13493e2cd46c5c0c8db1c15afa0d58e00000000"_base16);
    auto const data = data_chunk{
        0x46, 0x68, 0x24, 0x88, 0xf0, 0xa7, 0x21, 0x12, 0x4a, 0x39, 0x05, 0xa1,
        0xbb, 0x72, 0x44, 0x5b, 0xf1, 0x34, 0x93, 0xe2, 0xcd, 0x46, 0xc5, 0xc0,
        0xc8, 0xdb, 0x1c, 0x15, 0xaf, 0xa0, 0xd5, 0x8e, 0x00, 0x00, 0x00, 0x00};

    REQUIRE(raw == data);

    byte_reader reader(raw);
    auto result_exp = chain::point::from_data(reader);
    REQUIRE(result_exp);
    auto const point = std::move(*result_exp);
    REQUIRE(encode_hash(point.hash()) == "8ed5a0af151cdbc8c0c546cde29334f15b4472bba105394a1221a7f088246846");
    REQUIRE(point.index() == 0);

    data_chunk output = kth::to_data_chunk(point, true);
    REQUIRE(output == raw);
}



TEST_CASE("point operator assign equals 1 always matches equivalent", "[point]") {
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::point instance{null_hash, 0u};

    // This must be non-const.
    chain::point value{null_hash, 0u};

    reader.reset();
    result = chain::point::from_data(reader);
    REQUIRE(result);
    value = std::move(*result);
    instance = std::move(value);
    REQUIRE(instance == expected);
}

TEST_CASE("point operator assign equals 2 always matches equivalent", "[point]") {
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::point instance{null_hash, 0u};
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("point operator boolean equals duplicates returns true", "[point]") {
    chain::point alpha{null_hash, 0u};
    chain::point beta{null_hash, 0u};
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::point::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("point operator boolean equals differs returns false", "[point]") {
    chain::point alpha{null_hash, 0u};
    chain::point beta{null_hash, 0u};
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("point operator boolean not equals duplicates returns false", "[point]") {
    chain::point alpha{null_hash, 0u};
    chain::point beta{null_hash, 0u};
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::point::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("point operator boolean not equals differs returns true", "[point]") {
    chain::point alpha{null_hash, 0u};
    chain::point beta{null_hash, 0u};
    byte_reader reader(valid_raw_point);
    auto result = chain::point::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("point checksum all ones returns all ones", "[point]") {
    static auto const tx_hash = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_hash;
    chain::point instance(tx_hash, 0xffffffff);
    REQUIRE(instance.checksum() == 0xffffffffffffffff);
}

TEST_CASE("point checksum all zeroes returns all zeroes", "[point]") {
    static auto const tx_hash = "0000000000000000000000000000000000000000000000000000000000000000"_hash;
    chain::point instance(tx_hash, 0x00000000);
    REQUIRE(instance.checksum() == 0x0000000000000000);
}

TEST_CASE("point checksum pattern one returns expected", "[point]") {
    static auto const tx_hash = "000000000000000000000000aaaaaaaaaaaaaaaa000000000000000000000000"_hash;
    chain::point instance(tx_hash, 0x00000001);
    REQUIRE(instance.checksum() == 0xaaaaaaaaaaaa8001);
}

TEST_CASE("point checksum pattern high returns expected", "[point]") {
    static auto const tx_hash = "ffffffffffffffffffffffff01234567aaaaaaaaffffffffffffffffffffffff"_hash;
    chain::point instance(tx_hash, 0x89abcdef);
    REQUIRE(instance.checksum() == 0x1234567aaaacdef);
}

// End Test Suite
