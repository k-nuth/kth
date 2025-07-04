// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: message header tests

TEST_CASE("message header constructor 1  always initialized invalid", "[message header]") {
    message::header instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("message header  constructor 2  always  equals params", "[message header]") {
    uint32_t version = 10u;
    hash_digest previous = hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    hash_digest merkle = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    uint32_t timestamp = 531234u;
    uint32_t bits = 6523454u;
    uint32_t nonce = 68644u;

    message::header instance(version, previous, merkle, timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("message header  constructor 3  always  equals params", "[message header]") {
    uint32_t version = 10u;
    hash_digest previous = hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    hash_digest merkle = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    uint32_t timestamp = 531234u;
    uint32_t bits = 6523454u;
    uint32_t nonce = 68644u;

    message::header instance(version, std::move(previous), std::move(merkle), timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("message header  constructor 4  always  equals params", "[message header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        1234u);

    message::header instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("message header  constructor 5  always  equals params", "[message header]") {
    chain::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        123u);

    message::header instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("message header  constructor 6  always  equals params", "[message header]") {
    const message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("message header  constructor 7  always  equals params", "[message header]") {
    message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("message header from data insufficient bytes  failure", "[message header]") {
    data_chunk data(10);
    message::header header;
    byte_reader reader(data);
    auto result = message::header::from_data(reader, message::header::version_maximum);
    REQUIRE( ! result);
    REQUIRE( ! header.is_valid());
}

TEST_CASE("message header from data valid input canonical version  no transaction count", "[message header]") {
    auto const version = message::version::level::canonical;
    message::header expected{
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u};

    auto const data = expected.to_data(version);

    byte_reader reader(data);
    auto const result_exp = message::header::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == chain::header::satoshi_fixed_size());
}

TEST_CASE("message header from data valid input  success", "[message header]") {
    auto const version = message::header::version_minimum;
    message::header expected{
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u};

    auto const data = expected.to_data(version);

    byte_reader reader(data);
    auto const result_exp = message::header::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}



TEST_CASE("message header  operator assign equals 1  always  matches equivalent", "[message header]") {
    chain::header value(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    REQUIRE(value.is_valid());

    message::header instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("message header  operator assign equals 2  always  matches equivalent", "[message header]") {
    message::header value(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    REQUIRE(value.is_valid());

    message::header instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("message header  operator boolean equals 1  duplicates  returns true", "[message header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        3565u);

    message::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("message header  operator boolean equals 1  differs  returns false", "[message header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        6523454u,
        68644u,
        4453u);

    message::header instance;
    REQUIRE(instance != expected);
}

TEST_CASE("message header  operator boolean not equals 1  duplicates  returns false", "[message header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        6523454u,
        68644u,
        2345u);

    message::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("message header  operator boolean not equals 1  differs  returns true", "[message header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        68644u,
        47476u);

    message::header instance;
    REQUIRE(instance != expected);
}

TEST_CASE("message header  operator boolean equals 2  duplicates  returns true", "[message header]") {
    const message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("message header  operator boolean equals 2  differs  returns false", "[message header]") {
    const message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance;
    REQUIRE(instance != expected);
}

TEST_CASE("message header  operator boolean not equals 2  duplicates  returns false", "[message header]") {
    const message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("message header  operator boolean not equals 2  differs  returns true", "[message header]") {
    const message::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    message::header instance;
    REQUIRE(instance != expected);
}

// End Test Suite
