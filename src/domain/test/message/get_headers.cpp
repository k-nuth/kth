// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: get headers tests

TEST_CASE("get headers  constructor 1  always invalid", "[get headers]") {
    message::get_headers instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("get headers  constructor 2  always  equals params", "[get headers]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_headers instance(starts, stop);
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get headers  constructor 3  always  equals params", "[get headers]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};
    hash_list starts_duplicate = starts;

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_headers instance(std::move(starts_duplicate), std::move(stop));
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get headers  constructor 4  always  equals params", "[get headers]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    const message::get_headers expected(starts, stop);
    message::get_headers instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get headers  constructor 5  always  equals params", "[get headers]") {
    hash_list starts = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};

    hash_digest stop = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

    message::get_headers expected(starts, stop);
    message::get_headers instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(starts == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get headers from data insufficient bytes failure", "[get headers]") {
    data_chunk const raw {0xab, 0xcd};
    message::get_headers instance;

    byte_reader reader(raw);
    auto result = message::get_headers::from_data(reader, message::get_headers::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("get headers from data insufficient version failure", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    auto const data = expected.to_data(message::get_headers::version_minimum);
    message::get_headers instance{};

    byte_reader reader(data);
    auto result = message::get_headers::from_data(reader, message::get_headers::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("get headers from data valid input  success", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    auto const data = expected.to_data(message::get_headers::version_minimum);
    byte_reader reader(data);
    auto const result_exp = message::get_headers::from_data(reader, message::get_headers::version_minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::get_headers::version_minimum));
    REQUIRE(
        expected.serialized_size(message::get_headers::version_minimum) ==
        result.serialized_size(message::get_headers::version_minimum));
}



TEST_CASE("get headers  operator assign equals  always  matches equivalent", "[get headers]") {
    hash_list const start = {
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
    };

    hash_digest const stop = hash_literal("7777777777777777777777777777777777777777777777777777777777777777");

    message::get_headers value{start, stop};

    REQUIRE(value.is_valid());

    message::get_headers instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(start == instance.start_hashes());
    REQUIRE(stop == instance.stop_hash());
}

TEST_CASE("get headers  operator boolean equals  duplicates  returns true", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    message::get_headers instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get headers  operator boolean equals  differs  returns false", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    message::get_headers instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get headers  operator boolean not equals  duplicates  returns false", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    message::get_headers instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get headers  operator boolean not equals  differs  returns true", "[get headers]") {
    message::get_headers const expected {
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            hash_literal("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
            hash_literal("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
            hash_literal("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
        }, hash_literal("7777777777777777777777777777777777777777777777777777777777777777")
    };

    message::get_headers instance;
    REQUIRE(instance != expected);
}

// End Test Suite
