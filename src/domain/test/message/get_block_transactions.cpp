// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: get block transactions tests

TEST_CASE("get block transactions  constructor 1  always invalid", "[get block transactions]") {
    message::get_block_transactions instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("get block transactions  constructor 2  always  equals params", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    message::get_block_transactions instance(hash, indexes);
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  constructor 3  always  equals params", "[get block transactions]") {
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    auto hash_dup = hash;
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto indexes_dup = indexes;

    message::get_block_transactions instance(std::move(hash_dup), std::move(indexes_dup));
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  constructor 4  always  equals params", "[get block transactions]") {
    message::get_block_transactions value(
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {1u, 3454u, 4234u, 75123u, 455323u});

    message::get_block_transactions instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("get block transactions  constructor 5  always  equals params", "[get block transactions]") {
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    message::get_block_transactions value(hash, indexes);
    message::get_block_transactions instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions from data insufficient bytes  failure", "[get block transactions]") {
    data_chunk const raw{0xab, 0xcd};
    message::get_block_transactions instance{};

    byte_reader reader(raw);
    auto result = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("get block transactions from data valid input  success", "[get block transactions]") {
    const message::get_block_transactions expected{
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {16,
         32,
         37,
         44}};

    auto const data = expected.to_data(message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}



TEST_CASE("get block transactions  block hash accessor 1  always  returns initialized value", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    message::get_block_transactions instance(hash, indexes);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions  block hash accessor 2  always  returns initialized value", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    const message::get_block_transactions instance(hash, indexes);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions  block hash setter 1  roundtrip  success", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::get_block_transactions instance;
    REQUIRE(hash != instance.block_hash());
    instance.set_block_hash(hash);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions  block hash setter 2  roundtrip  success", "[get block transactions]") {
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    auto dup = hash;
    message::get_block_transactions instance;
    REQUIRE(hash != instance.block_hash());
    instance.set_block_hash(std::move(dup));
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions  indexes accessor 1  always  returns initialized value", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    message::get_block_transactions instance(hash, indexes);
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  indexes accessor 2  always  returns initialized value", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    const message::get_block_transactions instance(hash, indexes);
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  indexes setter 1  roundtrip  success", "[get block transactions]") {
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    message::get_block_transactions instance;
    REQUIRE(indexes != instance.indexes());
    instance.set_indexes(indexes);
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  indexes setter 2  roundtrip  success", "[get block transactions]") {
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto dup = indexes;
    message::get_block_transactions instance;
    REQUIRE(indexes != instance.indexes());
    instance.set_indexes(std::move(dup));
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  operator assign equals  always  matches equivalent", "[get block transactions]") {
    hash_digest const hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    message::get_block_transactions value(hash, indexes);

    REQUIRE(value.is_valid());

    message::get_block_transactions instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions  operator boolean equals  duplicates  returns true", "[get block transactions]") {
    const message::get_block_transactions expected(
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {1u, 3454u, 4234u, 75123u, 455323u});

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions  operator boolean equals  differs  returns false", "[get block transactions]") {
    const message::get_block_transactions expected(
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {1u, 3454u, 4234u, 75123u, 455323u});

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get block transactions  operator boolean not equals  duplicates  returns false", "[get block transactions]") {
    const message::get_block_transactions expected(
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {1u, 3454u, 4234u, 75123u, 455323u});

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions  operator boolean not equals  differs  returns true", "[get block transactions]") {
    const message::get_block_transactions expected(
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        {1u, 3454u, 4234u, 75123u, 455323u});

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

// End Test Suite
