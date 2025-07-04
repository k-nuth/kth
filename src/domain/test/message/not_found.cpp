// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: not found tests

TEST_CASE("not found  constructor 1  always invalid", "[not found]") {
    message::not_found instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("not found  constructor 2  always  equals params", "[not found]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(
                inventory::type_id::error,
                {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
                  0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
                  0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
                  0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}})};

    message::not_found instance(values);
    REQUIRE(instance.is_valid());
    REQUIRE(values == instance.inventories());
}

TEST_CASE("not found  constructor 3  always  equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::inventory_vector::list values =
        {
            message::inventory_vector(type, hash)};

    message::not_found instance(std::move(values));
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found  constructor 4  always  equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    hash_list const hashes = {hash};

    message::not_found instance(hashes, type);
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found  constructor 5  always  equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::not_found instance{{type, hash}};
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found  constructor 6  always  equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    const message::not_found value{{type, hash}};
    REQUIRE(value.is_valid());
    message::not_found instance(value);
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
    REQUIRE(value == instance);
}

TEST_CASE("not found  constructor 7  always  equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::not_found value{{type, hash}};
    REQUIRE(value.is_valid());
    message::not_found instance(std::move(value));
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found from data insufficient bytes  failure", "[not found]") {
    static data_chunk const raw{0xab, 0xcd};
    not_found instance;
    byte_reader reader(raw);
    auto result = not_found::from_data(reader, version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("not found from data insufficient version  failure", "[not found]") {
    static not_found const expected{
        {{inventory_vector::type_id::error,
          {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
            0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
            0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
            0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}}};

    auto const version = version::level::maximum;
    data_chunk const raw = expected.to_data(version);
    not_found instance;
    byte_reader reader(raw);
    auto result = not_found::from_data(reader, not_found::version_minimum - 1);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("not found from data valid input  success", "[not found]") {
    static not_found const expected{
        {{inventory_vector::type_id::error,
          {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
            0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
            0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
            0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}}};

    auto const version = version::level::maximum;
    auto const data = expected.to_data(version);
    byte_reader reader(data);
    auto const result_exp = not_found::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}



TEST_CASE("not found  operator assign equals  always  matches equivalent", "[not found]") {
    const message::inventory_vector::list elements =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))};

    message::not_found value(elements);
    REQUIRE(value.is_valid());

    message::not_found instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(elements == instance.inventories());
}

TEST_CASE("not found  operator boolean equals  duplicates  returns true", "[not found]") {
    const message::not_found expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::not_found instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("not found  operator boolean equals  differs  returns false", "[not found]") {
    const message::not_found expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::not_found instance;
    REQUIRE(instance != expected);
}

TEST_CASE("not found  operator boolean not equals  duplicates  returns false", "[not found]") {
    const message::not_found expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::not_found instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("not found  operator boolean not equals  differs  returns true", "[not found]") {
    const message::not_found expected(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"))});

    message::not_found instance;
    REQUIRE(instance != expected);
}

// End Test Suite
