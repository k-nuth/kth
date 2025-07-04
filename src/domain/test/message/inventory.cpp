// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: inventory tests

TEST_CASE("inventory  constructor 1  always invalid", "[inventory]") {
    message::inventory instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("inventory  constructor 2  always  equals params", "[inventory]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(
                inventory::type_id::error,
                {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
                  0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
                  0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
                  0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}})};

    message::inventory instance(values);
    REQUIRE(instance.is_valid());
    REQUIRE(values == instance.inventories());
}

TEST_CASE("inventory  constructor 3  always  equals params", "[inventory]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::inventory_vector::list values =
        {
            message::inventory_vector(type, hash)};

    message::inventory instance(std::move(values));
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("inventory  constructor 4  always  equals params", "[inventory]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    hash_list const hashes = {hash};

    message::inventory instance(hashes, type);
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("inventory  constructor 5  always  equals params", "[inventory]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::inventory instance{{type, hash}};
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("inventory  constructor 6  always  equals params", "[inventory]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    const message::inventory value{{type, hash}};
    REQUIRE(value.is_valid());
    message::inventory instance(value);
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
    REQUIRE(value == instance);
}

TEST_CASE("inventory  constructor 7  always  equals params", "[inventory]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::inventory value{{type, hash}};
    REQUIRE(value.is_valid());
    message::inventory instance(std::move(value));
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("inventory from data insufficient bytes  failure", "[inventory]") {
    static auto const version = version::level::minimum;
    static data_chunk const raw{0xab, 0xcd};
    inventory instance;
    byte_reader reader(raw);
    auto result = inventory::from_data(reader, version);
    REQUIRE( ! result);
}

TEST_CASE("inventory from data valid input  success", "[inventory]") {
    static inventory const expected{
        {{inventory::type_id::error,
          {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
            0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
            0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
            0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}}};

    static auto const version = version::level::minimum;
    auto const data = expected.to_data(version);
    byte_reader reader(data);
    auto const result_exp = inventory::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}



TEST_CASE("inventory  inventories accessor 1  always  returns initialized value", "[inventory]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    message::inventory instance(values);
    REQUIRE(values == instance.inventories());
}

TEST_CASE("inventory  inventories accessor 2  always  returns initialized value", "[inventory]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    const message::inventory instance(values);
    REQUIRE(values == instance.inventories());
}

TEST_CASE("inventory  inventories setter 1  roundtrip  success", "[inventory]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    message::inventory instance;
    REQUIRE(values != instance.inventories());
    instance.set_inventories(values);
    REQUIRE(values == instance.inventories());
}

TEST_CASE("inventory  inventories setter 2  roundtrip  success", "[inventory]") {
    message::inventory_vector::list values =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    message::inventory instance;
    REQUIRE(0u == instance.inventories().size());
    instance.set_inventories(std::move(values));
    REQUIRE(1u == instance.inventories().size());
}

TEST_CASE("inventory  operator assign equals  always  matches equivalent", "[inventory]") {
    const message::inventory_vector::list elements =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    message::inventory instance;
    REQUIRE( ! instance.is_valid());

    instance = message::inventory(elements);
    REQUIRE(instance.is_valid());
    REQUIRE(elements == instance.inventories());
}

TEST_CASE("inventory  operator boolean equals  duplicates  returns true", "[inventory]") {
    const message::inventory expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::inventory instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("inventory  operator boolean equals  differs  returns false", "[inventory]") {
    const message::inventory expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::inventory instance;
    REQUIRE(instance != expected);
}

TEST_CASE("inventory  operator boolean not equals  duplicates  returns false", "[inventory]") {
    const message::inventory expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::inventory instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("inventory  operator boolean not equals  differs  returns true", "[inventory]") {
    const message::inventory expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::inventory instance;
    REQUIRE(instance != expected);
}

TEST_CASE("inventory  count  no matching type  returns zero", "[inventory]") {
    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    REQUIRE(0u == instance.count(message::inventory_vector::type_id::block));
}

TEST_CASE("inventory  count  matching type  returns count", "[inventory]") {
    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")),
         message::inventory_vector(message::inventory_vector::type_id::block,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    REQUIRE(3u == instance.count(message::inventory_vector::type_id::error));
}

TEST_CASE("inventory  to hashes  matching type  returns empty list", "[inventory]") {
    hash_list const hashes = {};

    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("1111111111111111111111111111111111111111111111111111111111111111")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("2222222222222222222222222222222222222222222222222222222222222222")),
         message::inventory_vector(message::inventory_vector::type_id::block,
                                   hash_literal("3333333333333333333333333333333333333333333333333333333333333333")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4444444444444444444444444444444444444444444444444444444444444444"))});

    hash_list result;
    instance.to_hashes(result, message::inventory_vector::type_id::transaction);
    REQUIRE(hashes == result);
}

TEST_CASE("inventory  to hashes  matching type  returns hashes", "[inventory]") {
    hash_list const hashes = {
        hash_literal("1111111111111111111111111111111111111111111111111111111111111111"),
        hash_literal("2222222222222222222222222222222222222222222222222222222222222222"),
        hash_literal("4444444444444444444444444444444444444444444444444444444444444444")};

    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("1111111111111111111111111111111111111111111111111111111111111111")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("2222222222222222222222222222222222222222222222222222222222222222")),
         message::inventory_vector(message::inventory_vector::type_id::block,
                                   hash_literal("3333333333333333333333333333333333333333333333333333333333333333")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4444444444444444444444444444444444444444444444444444444444444444"))});

    hash_list result;
    instance.to_hashes(result, message::inventory_vector::type_id::error);
    REQUIRE(hashes == result);
}

TEST_CASE("inventory  reduce  matching type  returns empty list", "[inventory]") {
    const message::inventory_vector::list expected = {};

    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("1111111111111111111111111111111111111111111111111111111111111111")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("2222222222222222222222222222222222222222222222222222222222222222")),
         message::inventory_vector(message::inventory_vector::type_id::block,
                                   hash_literal("3333333333333333333333333333333333333333333333333333333333333333")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4444444444444444444444444444444444444444444444444444444444444444"))});

    message::inventory_vector::list result;
    instance.reduce(result, message::inventory_vector::type_id::transaction);
    REQUIRE(expected == result);
}

TEST_CASE("inventory  reduce  matching type  returns matches", "[inventory]") {
    const message::inventory_vector::list expected = {
        message::inventory_vector(message::inventory_vector::type_id::error,
                                  hash_literal("1111111111111111111111111111111111111111111111111111111111111111")),
        message::inventory_vector(message::inventory_vector::type_id::error,
                                  hash_literal("2222222222222222222222222222222222222222222222222222222222222222")),
        message::inventory_vector(message::inventory_vector::type_id::error,
                                  hash_literal("4444444444444444444444444444444444444444444444444444444444444444"))};

    message::inventory instance(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("1111111111111111111111111111111111111111111111111111111111111111")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("2222222222222222222222222222222222222222222222222222222222222222")),
         message::inventory_vector(message::inventory_vector::type_id::block,
                                   hash_literal("3333333333333333333333333333333333333333333333333333333333333333")),
         message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4444444444444444444444444444444444444444444444444444444444444444"))});

    message::inventory_vector::list result;
    instance.reduce(result, message::inventory_vector::type_id::error);
    REQUIRE(expected.size() == result.size());
    REQUIRE(expected == result);
}

// End Test Suite
