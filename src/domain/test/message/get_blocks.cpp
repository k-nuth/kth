// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: get blocks tests

TEST_CASE("get blocks  constructor 1  always invalid", "[get blocks]") {
    message::get_blocks instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("get blocks  constructor 2  always  equals params", "[get blocks]") {
    hash_list const starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_blocks instance(starts, stop);
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get blocks  constructor 3  always  equals params", "[get blocks]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};
    hash_list starts_duplicate = starts;

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_blocks instance(std::move(starts_duplicate), std::move(stop));
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get blocks  constructor 4  always  equals params", "[get blocks]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    const message::get_blocks expected(starts, stop);
    message::get_blocks instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get blocks  constructor 5  always  equals params", "[get blocks]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_blocks expected(starts, stop);
    message::get_blocks instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get blocks from data insufficient bytes  failure", "[get blocks]") {
    data_chunk const raw{0xab, 0xcd};
    message::get_blocks instance;

    byte_reader reader(raw);
    auto result = message::get_blocks::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("get blocks from data valid input  success", "[get blocks]") {
    const message::get_blocks expected{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    auto const data = expected.to_data(message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::get_blocks::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}



TEST_CASE("get blocks  start hashes accessor 1  always  returns initialized value", "[get blocks]") {
    hash_list expected = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    message::get_blocks instance{
        expected,
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    REQUIRE(expected == instance.start_hashes());
}

TEST_CASE("get blocks  start hashes accessor 2  always  returns initialized value", "[get blocks]") {
    hash_list expected = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    const message::get_blocks instance{
        expected,
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    REQUIRE(expected == instance.start_hashes());
}

TEST_CASE("get blocks  start hashes setter 1  roundtrip  success", "[get blocks]") {
    hash_list const values = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    message::get_blocks instance;
    REQUIRE(values != instance.start_hashes());
    instance.set_start_hashes(values);
    REQUIRE(values == instance.start_hashes());
}

TEST_CASE("get blocks  start hashes setter 2  roundtrip  success", "[get blocks]") {
    hash_list values = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_list values_duplicate = values;

    message::get_blocks instance;
    REQUIRE(values != instance.start_hashes());
    instance.set_start_hashes(std::move(values_duplicate));
    REQUIRE(values == instance.start_hashes());
}

TEST_CASE("get blocks  stop hash accessor 1  always  returns initialized value", "[get blocks]") {
    hash_digest expected = hash_literal(
        "7777777777777777777777777777777777777777777777777777777777777777");

    message::get_blocks instance{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        expected};

    REQUIRE(expected == instance.stop_hash());
}

TEST_CASE("get blocks  stop hash accessor 2  always  returns initialized value", "[get blocks]") {
    hash_digest expected = hash_literal(
        "7777777777777777777777777777777777777777777777777777777777777777");

    const message::get_blocks instance{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        expected};

    REQUIRE(expected == instance.stop_hash());
}

TEST_CASE("get blocks  stop hash setter 1  roundtrip  success", "[get blocks]") {
    hash_digest value = hash_literal("7777777777777777777777777777777777777777777777777777777777777777");
    message::get_blocks instance;
    REQUIRE(value != instance.stop_hash());
    instance.set_stop_hash(value);
    REQUIRE(value == instance.stop_hash());
}

TEST_CASE("get blocks  stop hash setter 2  roundtrip  success", "[get blocks]") {
    hash_digest value = hash_literal("7777777777777777777777777777777777777777777777777777777777777777");
    message::get_blocks instance;
    REQUIRE(value != instance.stop_hash());
    instance.set_stop_hash(std::move(value));
    REQUIRE(value == instance.stop_hash());
}

TEST_CASE("get blocks  operator assign equals  always  matches equivalent", "[get blocks]") {
    hash_list start = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("7777777777777777777777777777777777777777777777777777777777777777");

    message::get_blocks value{start, stop};

    REQUIRE(value.is_valid());

    message::get_blocks instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(start == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get blocks  operator boolean equals  duplicates  returns true", "[get blocks]") {
    const message::get_blocks expected{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    message::get_blocks instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get blocks  operator boolean equals  differs  returns false", "[get blocks]") {
    const message::get_blocks expected{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    message::get_blocks instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get blocks  operator boolean not equals  duplicates  returns false", "[get blocks]") {
    const message::get_blocks expected{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    message::get_blocks instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get blocks  operator boolean not equals  differs  returns true", "[get blocks]") {
    const message::get_blocks expected{
        {hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
         hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
         hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
         hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
         hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")},
        hash_literal("7777777777777777777777777777777777777777777777777777777777777777")};

    message::get_blocks instance;
    REQUIRE(instance != expected);
}

// End Test Suite
