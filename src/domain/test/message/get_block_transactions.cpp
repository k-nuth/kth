// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: get block transactions tests
TEST_CASE("get block transactions constructor 2 always equals params", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions constructor 3 always equals params", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    auto hash_dup = hash;
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto indexes_dup = indexes;

    auto const instance = message::get_block_transactions::create(hash_dup, std::move(indexes_dup)).value();
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions constructor 4 always equals params", "[get block transactions]") {
    auto const value = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(value);
    REQUIRE(value == instance);
}

TEST_CASE("get block transactions constructor 5 always equals params", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    auto value = message::get_block_transactions::create(hash, indexes).value();
    message::get_block_transactions instance(std::move(value));
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions from data insufficient bytes failure", "[get block transactions]") {
    data_chunk const raw{0xab, 0xcd};
    message::get_block_transactions instance{};

    byte_reader reader(raw);
    auto result = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("get block transactions from data valid input success", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {16,
         32,
         37,
         44}).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}

TEST_CASE("get block transactions block hash accessor 1 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash accessor 2 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash setter 1 roundtrip success", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    message::get_block_transactions const empty;
    REQUIRE(hash != empty.block_hash());

    auto const instance = message::get_block_transactions::create(hash, {}).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash setter 2 roundtrip success", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    auto dup = hash;
    message::get_block_transactions const empty;
    REQUIRE(hash != empty.block_hash());

    auto const instance = message::get_block_transactions::create(dup, {}).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions indexes accessor 1 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes accessor 2 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes are set at construction", "[get block transactions]") {
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    message::get_block_transactions const empty;
    REQUIRE(indexes != empty.indexes());

    auto const instance = message::get_block_transactions::create(null_hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes are set at construction, by move", "[get block transactions]") {
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto dup = indexes;

    message::get_block_transactions const empty;
    REQUIRE(indexes != empty.indexes());

    auto const instance = message::get_block_transactions::create(null_hash, std::move(dup)).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions operator assign equals always matches equivalent", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto value = message::get_block_transactions::create(hash, indexes).value();


    message::get_block_transactions instance;

    instance = std::move(value);
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions operator boolean equals duplicates returns true", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions operator boolean equals differs returns false", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get block transactions operator boolean not equals duplicates returns false", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions operator boolean not equals differs returns true", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

// End Test Suite
