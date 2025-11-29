// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: get data tests

TEST_CASE("get data  constructor 1  always invalid", "[get data]") {
    const get_data instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("get data  constructor 2  always  equals params", "[get data]") {
    static inventory_vector::list const values =
        {
            inventory_vector{
                inventory::type_id::error,
                {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
                  0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
                  0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
                  0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}};

    const get_data instance(values);
    REQUIRE(instance.is_valid());
    REQUIRE(values == instance.inventories());
}

TEST_CASE("get data  constructor 3  always  equals params", "[get data]") {
    static auto const type = inventory_vector::type_id::error;
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    inventory_vector::list const values{
        inventory_vector(type, hash)};

    const get_data instance(std::move(values));
    REQUIRE(instance.is_valid());
    auto const inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("get data  constructor 4  always  equals params", "[get data]") {
    static auto const type = inventory_vector::type_id::error;
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    static hash_list const hashes{hash};

    const get_data instance(hashes, type);
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("get data  constructor 5  always  equals params", "[get data]") {
    static auto const type = inventory_vector::type_id::error;
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    const get_data instance{{type, hash}};
    REQUIRE(instance.is_valid());
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("get data  constructor 6  always  equals params", "[get data]") {
    static auto const type = inventory_vector::type_id::error;
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    const get_data value{{type, hash}};
    REQUIRE(value.is_valid());
    const get_data instance(value);
    auto const inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
    REQUIRE(value == instance);
}

TEST_CASE("get data  constructor 7  always  equals params", "[get data]") {
    static auto const type = inventory_vector::type_id::error;
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    const get_data value{{type, hash}};
    REQUIRE(value.is_valid());
    const get_data instance(std::move(value));
    auto const inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("get data from data insufficient bytes  failure", "[get data]") {
    static data_chunk const raw{0xab, 0xcd};
    get_data instance;
    static auto const version = version::level::maximum;
    byte_reader reader(raw);
    auto result = get_data::from_data(reader, version);
    REQUIRE( ! result);
}

TEST_CASE("get data from data insufficient version  failure", "[get data]") {
    static const get_data expected{
        {inventory_vector::type_id::error,
         "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash}};

    auto const raw = expected.to_data(version::level::maximum);
    byte_reader reader(raw);
    auto const result = get_data::from_data(reader, get_data::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("get data from data valid input  success", "[get data]") {
    static const get_data expected{
        {inventory_vector::type_id::error,
         "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash}};

    static auto const version = version::level::maximum;
    auto const data = expected.to_data(version);
    byte_reader reader(data);
    auto const result_exp = get_data::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}



TEST_CASE("get data  operator assign equals  always  matches equivalent", "[get data]") {
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    static inventory_vector::list const elements{
        inventory_vector(inventory_vector::type_id::error, hash)};

    get_data value(elements);
    REQUIRE(value.is_valid());

    get_data instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(elements == instance.inventories());
}

TEST_CASE("get data  operator boolean equals  duplicates  returns true", "[get data]") {
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const get_data expected{
        inventory_vector(inventory_vector::type_id::error, hash)};

    const get_data instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get data  operator boolean equals  differs  returns false", "[get data]") {
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const get_data expected{
        inventory_vector(inventory_vector::type_id::error, hash)};

    const get_data instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get data  operator boolean not equals  duplicates  returns false", "[get data]") {
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const get_data expected{
        inventory_vector(inventory_vector::type_id::error, hash)};

    const get_data instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get data  operator boolean not equals  differs  returns true", "[get data]") {
    static auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const get_data expected{
        inventory_vector(inventory_vector::type_id::error, hash)};

    const get_data instance;
    REQUIRE(instance != expected);
}

// End Test Suite
