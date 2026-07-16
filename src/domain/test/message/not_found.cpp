// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: not found tests
TEST_CASE("not found constructor 2 always equals params", "[not found]") {
    const message::inventory_vector::list values =
        {
            message::inventory_vector(
                inventory::type_id::error,
                {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
                  0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
                  0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
                  0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}})};

    auto const instance = message::not_found::create(values).value();
    REQUIRE(values == instance.inventories());
}

TEST_CASE("not found constructor 3 always equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    message::inventory_vector::list values =
        {
            message::inventory_vector(type, hash)};

    auto const instance = message::not_found::create(std::move(values)).value();
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found constructor 4 always equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    hash_list const hashes = {hash};

    auto const instance = message::not_found::create(hashes, type).value();
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found constructor 5 always equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    auto const instance = message::not_found::create({message::inventory_vector(type, hash)}).value();
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found constructor 6 always equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    auto const value = message::not_found::create({message::inventory_vector(type, hash)}).value();
    message::not_found instance(value);
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
    REQUIRE(value == instance);
}

TEST_CASE("not found constructor 7 always equals params", "[not found]") {
    message::inventory_vector::type_id type = message::inventory_vector::type_id::error;
    auto hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    auto value = message::not_found::create({message::inventory_vector(type, hash)}).value();
    message::not_found instance(std::move(value));
    auto inventories = instance.inventories();
    REQUIRE(1u == inventories.size());
    REQUIRE(type == inventories[0].type());
    REQUIRE(hash == inventories[0].hash());
}

TEST_CASE("not found from data insufficient bytes failure", "[not found]") {
    static data_chunk const raw{0xab, 0xcd};
    not_found instance;
    byte_reader reader(raw);
    auto result = not_found::from_data(reader, version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("not found from data insufficient version failure", "[not found]") {
    static auto const expected = not_found::create(
        {{inventory_vector::type_id::error,
          {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
            0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
            0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
            0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}}).value();

    auto const version = version::level::maximum;
    data_chunk const raw = kth::to_data_chunk(expected, version);
    not_found instance;
    byte_reader reader(raw);
    auto result = not_found::from_data(reader, not_found::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("not found from data valid input success", "[not found]") {
    static auto const expected = not_found::create(
        {{inventory_vector::type_id::error,
          {{0x44, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
            0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
            0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
            0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}}}).value();

    auto const version = version::level::maximum;
    auto const data = kth::to_data_chunk(expected, version);
    byte_reader reader(data);
    auto const result_exp = not_found::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version));
    REQUIRE(expected.serialized_size(version) == result.serialized_size(version));
}

TEST_CASE("not found operator assign equals always matches equivalent", "[not found]") {
    const message::inventory_vector::list elements =
        {
            message::inventory_vector(message::inventory_vector::type_id::error,
                                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash)};

    auto value = message::not_found::create(elements).value();

    message::not_found instance;

    instance = std::move(value);
    REQUIRE(elements == instance.inventories());
}

TEST_CASE("not found operator boolean equals duplicates returns true", "[not found]") {
    auto const expected = message::not_found::create(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash)}).value();

    message::not_found instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("not found operator boolean equals differs returns false", "[not found]") {
    auto const expected = message::not_found::create(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash)}).value();

    message::not_found instance;
    REQUIRE(instance != expected);
}

TEST_CASE("not found operator boolean not equals duplicates returns false", "[not found]") {
    auto const expected = message::not_found::create(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash)}).value();

    message::not_found instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("not found operator boolean not equals differs returns true", "[not found]") {
    auto const expected = message::not_found::create(
        {message::inventory_vector(message::inventory_vector::type_id::error,
                                   "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash)}).value();

    message::not_found instance;
    REQUIRE(instance != expected);
}

// End Test Suite
