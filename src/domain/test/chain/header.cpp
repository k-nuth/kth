// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: chain header tests

TEST_CASE("chain header constructor 1 always initialized invalid", "[chain header]") {
    chain::header instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("chain header  constructor 2  always  equals params", "[chain header]") {
    uint32_t const version = 10u;
    auto const previous = hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    auto const merkle = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    uint32_t const timestamp = 531234u;
    uint32_t const bits = 6523454u;
    uint32_t const nonce = 68644u;

    chain::header instance(version, previous, merkle, timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("chain header  constructor 3  always  equals params", "[chain header]") {
    uint32_t const version = 10u;
    uint32_t const timestamp = 531234u;
    uint32_t const bits = 6523454u;
    uint32_t const nonce = 68644u;

    // These must be non-const.
    auto previous = hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    auto merkle = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    chain::header instance(version, std::move(previous), std::move(merkle), timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("chain header  constructor 4  always  equals params", "[chain header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("chain header  constructor 5  always  equals params", "[chain header]") {
    // This must be non-const.
    chain::header expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("chain header from data insufficient bytes  failure", "[chain header]") {
    data_chunk data(10);
    byte_reader reader(data);
    auto const result = chain::header::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("chain header from data valid input  success", "[chain header]") {
    chain::header expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();

    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  factory from data 2  valid input  success", "[chain header]") {
    chain::header expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();
    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  factory from data 3  valid input  success", "[chain header]") {
    chain::header const expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();
    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  version accessor  always  returns initialized value", "[chain header]") {
    uint32_t const value = 11234u;
    chain::header const instance(
        value,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.version());
}

TEST_CASE("chain header  version setter  roundtrip  success", "[chain header]") {
    uint32_t expected = 4521u;
    chain::header instance;
    REQUIRE(expected != instance.version());
    instance.set_version(expected);
    REQUIRE(expected == instance.version());
}

TEST_CASE("chain header  previous block hash accessor 1  always  returns initialized value", "[chain header]") {
    auto const value = hash_literal("abababababababababababababababababababababababababababababababab");
    chain::header instance(
        11234u,
        value,
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.previous_block_hash());
}

TEST_CASE("chain header  previous block hash accessor 2  always  returns initialized value", "[chain header]") {
    auto const value = hash_literal("abababababababababababababababababababababababababababababababab");
    chain::header const instance(
        11234u,
        value,
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.previous_block_hash());
}

TEST_CASE("chain header  previous block hash setter 1  roundtrip  success", "[chain header]") {
    auto const expected = hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe");
    chain::header instance;
    REQUIRE(expected != instance.previous_block_hash());
    instance.set_previous_block_hash(expected);
    REQUIRE(expected == instance.previous_block_hash());
}

TEST_CASE("chain header  previous block hash setter 2  roundtrip  success", "[chain header]") {
    auto const expected = hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe");

    // This must be non-const.
    auto duplicate = expected;

    chain::header instance;
    REQUIRE(expected != instance.previous_block_hash());
    instance.set_previous_block_hash(std::move(duplicate));
    REQUIRE(expected == instance.previous_block_hash());
}

TEST_CASE("chain header  merkle accessor 1  always  returns initialized value", "[chain header]") {
    auto const value = hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe");
    chain::header instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        value,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.merkle());
}

TEST_CASE("chain header  merkle accessor 2  always  returns initialized value", "[chain header]") {
    auto const value = hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe");
    chain::header const instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        value,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.merkle());
}

TEST_CASE("chain header  merkle setter 1  roundtrip  success", "[chain header]") {
    auto const expected = hash_literal("abababababababababababababababababababababababababababababababab");
    chain::header instance;
    REQUIRE(expected != instance.merkle());
    instance.set_merkle(expected);
    REQUIRE(expected == instance.merkle());
}

TEST_CASE("chain header  merkle setter 2  roundtrip  success", "[chain header]") {
    auto const expected = hash_literal("abababababababababababababababababababababababababababababababab");

    // This must be non-const.
    hash_digest duplicate = expected;

    chain::header instance;
    REQUIRE(expected != instance.merkle());
    instance.set_merkle(std::move(duplicate));
    REQUIRE(expected == instance.merkle());
}

TEST_CASE("chain header  timestamp accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 753234u;
    chain::header instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        value,
        4356344u,
        34564u);

    REQUIRE(value == instance.timestamp());
}

TEST_CASE("chain header  timestamp setter  roundtrip  success", "[chain header]") {
    uint32_t expected = 4521u;
    chain::header instance;
    REQUIRE(expected != instance.timestamp());
    instance.set_timestamp(expected);
    REQUIRE(expected == instance.timestamp());
}

TEST_CASE("chain header  bits accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 4356344u;
    chain::header instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        value,
        34564u);

    REQUIRE(value == instance.bits());
}

TEST_CASE("chain header  bits setter  roundtrip  success", "[chain header]") {
    uint32_t expected = 4521u;
    chain::header instance;
    REQUIRE(expected != instance.bits());
    instance.set_bits(expected);
    REQUIRE(expected == instance.bits());
}

TEST_CASE("chain header  nonce accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 34564u;
    chain::header instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        4356344u,
        value);

    REQUIRE(value == instance.nonce());
}

TEST_CASE("chain header  nonce setter  roundtrip  success", "[chain header]") {
    uint32_t expected = 4521u;
    chain::header instance;
    REQUIRE(expected != instance.nonce());
    instance.set_nonce(expected);
    REQUIRE(expected == instance.nonce());
}

TEST_CASE("chain header  is valid timestamp  timestamp less than 2 hours from now  returns true", "[chain header]") {
    chain::header instance;
    auto const now = std::chrono::system_clock::now();
    auto const now_time = std::chrono::system_clock::to_time_t(now);
    instance.set_timestamp(static_cast<uint32_t>(now_time));
    REQUIRE(instance.is_valid_timestamp());
}

TEST_CASE("chain header  is valid timestamp  timestamp greater than 2 hours from now  returns false", "[chain header]") {
    chain::header instance;
    auto const now = std::chrono::system_clock::now();
    auto const duration = std::chrono::hours(3);
    auto const future = std::chrono::system_clock::to_time_t(now + duration);
    instance.set_timestamp(static_cast<uint32_t>(future));
    REQUIRE( ! instance.is_valid_timestamp());
}

TEST_CASE("chain header  proof1  genesis mainnet  expected", "[chain header]") {
    REQUIRE(chain::header::proof(0x1d00ffff) == 0x0000000100010001);
}

TEST_CASE("chain header  is valid proof of work  bits exceeds maximum  returns false", "[chain header]") {
    chain::header instance;
    instance.set_bits(retarget_proof_of_work_limit + 1);
    REQUIRE( ! instance.is_valid_proof_of_work());
}

TEST_CASE("chain header  is valid proof of work  retarget bits exceeds maximum  returns false", "[chain header]") {
    chain::header instance;
    instance.set_bits(no_retarget_proof_of_work_limit + 1);
    REQUIRE( ! instance.is_valid_proof_of_work(false));
}

TEST_CASE("chain header  is valid proof of work  hash greater bits  returns false", "[chain header]") {
    chain::header const instance(
        11234u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
        753234u,
        0u,
        34564u);

    REQUIRE( ! instance.is_valid_proof_of_work());
}

TEST_CASE("chain header  is valid proof of work  hash less than bits  returns true", "[chain header]") {
    chain::header const instance(
        4u,
        hash_literal("000000000000000003ddc1e929e2944b8b0039af9aa0d826c480a83d8b39c373"),
        hash_literal("a6cb0b0d6531a71abe2daaa4a991e5498e1b6b0b51549568d0f9d55329b905df"),
        1474388414u,
        402972254u,
        2842832236u);

    REQUIRE(instance.is_valid_proof_of_work());
}

TEST_CASE("chain header  operator assign equals  always  matches equivalent", "[chain header]") {
    // This must be non-const.
    chain::header value(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    REQUIRE(value.is_valid());

    chain::header instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("chain header  operator boolean equals  duplicates  returns true", "[chain header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("chain header  operator boolean equals  differs  returns false", "[chain header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance;
    REQUIRE(instance != expected);
}

TEST_CASE("chain header  operator boolean not equals  duplicates  returns false", "[chain header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("chain header  operator boolean not equals  differs  returns true", "[chain header]") {
    chain::header const expected(
        10u,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234u,
        6523454u,
        68644u);

    chain::header instance;
    REQUIRE(instance != expected);
}

// End Test Suite
