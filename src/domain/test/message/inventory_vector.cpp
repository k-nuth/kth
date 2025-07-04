// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: inventory vector tests

TEST_CASE("inventory vector to number error returns 0", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::error) == 0u);
}

TEST_CASE("inventory vector to number transaction returns 1", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::transaction) == 1u);
}

TEST_CASE("inventory vector to number block returns 2", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::block) == 2u);
}

TEST_CASE("inventory vector to number filtered block returns 3", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::filtered_block) == 3u);
}

TEST_CASE("inventory vector to number compact block returns 4", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::compact_block) == 4u);
}

TEST_CASE("inventory vector to number double spend proofs returns 0x94a0", "[inventory vector]") {
    REQUIRE(inventory_vector::to_number(inventory_vector::type_id::double_spend_proof) == 0x94a0u);
}

TEST_CASE("inventory vector to type 0 returns error", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::error == inventory_vector::to_type(0));
}

TEST_CASE("inventory vector to type 1 returns transaction", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::transaction == inventory_vector::to_type(1));
}

TEST_CASE("inventory vector to type 2 returns block", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::block == inventory_vector::to_type(2));
}

TEST_CASE("inventory vector to type 3 returns filtered block", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::filtered_block == inventory_vector::to_type(3));
}

TEST_CASE("inventory vector to type 4 returns compact block", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::compact_block == inventory_vector::to_type(4));
}

TEST_CASE("inventory vector to type 0x94a0 returns double spend proofs", "[inventory vector]") {
    REQUIRE(inventory_vector::type_id::double_spend_proof == inventory_vector::to_type(0x94a0));
}

TEST_CASE("inventory vector constructor 1 always invalid", "[inventory vector]") {
    inventory_vector instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("inventory vector constructor 2 always equals params", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::block;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance(type, hash);
    REQUIRE(instance.is_valid());
    REQUIRE(type == instance.type());
    REQUIRE(hash == instance.hash());
}

TEST_CASE("inventory vector constructor 3 always equals params", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::block;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance(type, std::move(hash));
    REQUIRE(instance.is_valid());
}

TEST_CASE("inventory vector constructor 4 always equals params", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::block;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector const expected(type, hash);
    REQUIRE(expected.is_valid());
    inventory_vector instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("inventory vector constructor 5 always equals params", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::block;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector expected(type, hash);
    REQUIRE(expected.is_valid());
    inventory_vector instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(type == instance.type());
    REQUIRE(hash == instance.hash());
}

TEST_CASE("inventory vector from data  insufficient bytes  failure", "[inventory vector]") {
    static data_chunk const raw{1};
    inventory_vector instance;
    byte_reader reader(raw);
    auto result = inventory_vector::from_data(reader, version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("inventory vector from data  valid input  success", "[inventory vector]") {
    static inventory_vector const expected{
        inventory_vector::type_id::error,
        {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
          0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
          0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
          0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}};

    static auto const version = version::level::minimum;
    auto const data = expected.to_data(version);
    byte_reader reader(data);
    auto const result_exp = inventory_vector::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}



TEST_CASE("inventory vector is block type block returns true", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::block);
    REQUIRE(instance.is_block_type());
}

TEST_CASE("inventory vector is block type compact block returns true", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::compact_block);
    REQUIRE(instance.is_block_type());
}

TEST_CASE("inventory vector is block type double spend proof returns false", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::double_spend_proof);
    REQUIRE( ! instance.is_block_type());
}

TEST_CASE("inventory vector is block type filtered block returns true", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::filtered_block);
    REQUIRE(instance.is_block_type());
}

TEST_CASE("inventory vector is block type non block type returns false", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::transaction);
    REQUIRE( ! instance.is_block_type());
}

TEST_CASE("inventory vector is transaction type transaction returns true", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::transaction);
    REQUIRE(instance.is_transaction_type());
}

TEST_CASE("inventory vector is transaction type non transaction type returns false", "[inventory vector]") {
    inventory_vector instance;
    instance.set_type(inventory_vector::type_id::block);
    REQUIRE( ! instance.is_transaction_type());
}

TEST_CASE("inventory vector type accessor always returns initialized value", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::transaction;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance(type, hash);
    REQUIRE(type == instance.type());
}

TEST_CASE("inventory vector type setter  roundtrip  success", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::transaction;
    inventory_vector instance;
    REQUIRE(type != instance.type());
    instance.set_type(type);
    REQUIRE(type == instance.type());
}

TEST_CASE("inventory vector hash accessor always returns initialized value", "[inventory vector]") {
    inventory_vector::type_id type = inventory_vector::type_id::transaction;
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance(type, hash);
    REQUIRE(hash == instance.hash());
}

TEST_CASE("inventory vector hash setter 1  roundtrip  success", "[inventory vector]") {
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance;
    REQUIRE(hash != instance.hash());
    instance.set_hash(hash);
    REQUIRE(hash == instance.hash());
}

TEST_CASE("inventory vector hash setter 2  roundtrip  success", "[inventory vector]") {
    hash_digest duplicate = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    hash_digest hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    inventory_vector instance;
    REQUIRE(duplicate != instance.hash());
    instance.set_hash(std::move(hash));
    REQUIRE(duplicate == instance.hash());
}

TEST_CASE("inventory vector operator assign equals 1 always matches equivalent", "[inventory vector]") {
    inventory_vector value(
        inventory_vector::type_id::compact_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    REQUIRE(value.is_valid());

    inventory_vector instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("inventory vector operator assign equals 2 always matches equivalent", "[inventory vector]") {
    inventory_vector value(
        inventory_vector::type_id::compact_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    REQUIRE(value.is_valid());

    inventory_vector instance;
    REQUIRE( ! instance.is_valid());

    instance = value;
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("inventory vector operator boolean equals duplicates returns true", "[inventory vector]") {
    inventory_vector const expected(
        inventory_vector::type_id::filtered_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    inventory_vector instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("inventory vector operator boolean equals differs returns false", "[inventory vector]") {
    inventory_vector const expected(
        inventory_vector::type_id::filtered_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    inventory_vector instance;
    REQUIRE(instance != expected);
}

TEST_CASE("inventory vector - reject  operator boolean not equals duplicates returns false", "[inventory vector]") {
    inventory_vector const expected(
        inventory_vector::type_id::filtered_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    inventory_vector instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("inventory vector - reject  operator boolean not equals differs returns true", "[inventory vector]") {
    inventory_vector const expected(
        inventory_vector::type_id::filtered_block,
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    inventory_vector instance;
    REQUIRE(instance != expected);
}

// End Test Suite
