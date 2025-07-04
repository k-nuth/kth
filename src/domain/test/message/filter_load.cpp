// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: filter load tests

TEST_CASE("filter load  constructor 1  always invalid", "[filter load]") {
    message::filter_load instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("filter load  constructor 2  always  equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(instance.is_valid());
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  constructor 3  always  equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    auto dup_filter = filter;
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(std::move(dup_filter), hash_functions, tweak, flags);
    REQUIRE(instance.is_valid());
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  constructor 4  always  equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    const message::filter_load value(filter, hash_functions, tweak, flags);
    message::filter_load instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  constructor 5  always  equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load value(filter, hash_functions, tweak, flags);
    message::filter_load instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load from data insufficient bytes  failure", "[filter load]") {
    data_chunk const raw{0xab, 0x11};
    message::filter_load instance;

    byte_reader reader(raw);
    auto result = message::filter_load::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("filter load from data insufficient version  failure", "[filter load]") {
    const message::filter_load expected {
        {0x05, 0xaa, 0xbb, 0xcc, 0xdd, 0xee},
        25,
        10,
        0xab
    };

    data_chunk const data = expected.to_data(message::version::level::maximum);
    
    byte_reader reader(data);
    auto result = message::filter_load::from_data(reader, message::filter_load::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("filter load from data valid input  success", "[filter load]") {
    const message::filter_load expected{
        {0x05, 0xaa, 0xbb, 0xcc, 0xdd, 0xee},
        25,
        10,
        0xab};

    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::filter_load::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::maximum));
    REQUIRE(expected.serialized_size(message::version::level::maximum) ==
                        result.serialized_size(message::version::level::maximum));
}



TEST_CASE("filter load  filter accessor 1  always  returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load  filter accessor 2  always  returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    const message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load  filter setter 1  roundtrip  success", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    message::filter_load instance;
    REQUIRE(filter != instance.filter());
    instance.set_filter(filter);
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load  filter setter 2  roundtrip  success", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    data_chunk dup = filter;

    message::filter_load instance;
    REQUIRE(filter != instance.filter());
    instance.set_filter(std::move(dup));
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load  hash functions accessor  always  returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(hash_functions == instance.hash_functions());
}

TEST_CASE("filter load  hash functions setter  roundtrip  success", "[filter load]") {
    uint32_t hash_functions = 48u;
    message::filter_load instance;
    REQUIRE(hash_functions != instance.hash_functions());
    instance.set_hash_functions(hash_functions);
    REQUIRE(hash_functions == instance.hash_functions());
}

TEST_CASE("filter load  tweak accessor  always  returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(tweak == instance.tweak());
}

TEST_CASE("filter load  tweak setter  roundtrip  success", "[filter load]") {
    uint32_t tweak = 36u;
    message::filter_load instance;
    REQUIRE(tweak != instance.tweak());
    instance.set_tweak(tweak);
    REQUIRE(tweak == instance.tweak());
}

TEST_CASE("filter load  flags accessor  always  returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    message::filter_load instance(filter, hash_functions, tweak, flags);
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  flags setter  roundtrip  success", "[filter load]") {
    uint8_t flags = 0xae;
    message::filter_load instance;
    REQUIRE(flags != instance.flags());
    instance.set_flags(flags);
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  operator assign equals  always  matches equivalent", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;
    message::filter_load value(filter, hash_functions, tweak, flags);

    REQUIRE(value.is_valid());

    message::filter_load instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load  operator boolean equals  duplicates  returns true", "[filter load]") {
    const message::filter_load expected(
        {0x0f, 0xf0, 0x55, 0xaa}, 643u, 575u, 0xaa);

    message::filter_load instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter load  operator boolean equals  differs  returns false", "[filter load]") {
    const message::filter_load expected(
        {0x0f, 0xf0, 0x55, 0xaa}, 643u, 575u, 0xaa);

    message::filter_load instance;
    REQUIRE(instance != expected);
}

TEST_CASE("filter load  operator boolean not equals  duplicates  returns false", "[filter load]") {
    const message::filter_load expected(
        {0x0f, 0xf0, 0x55, 0xaa}, 643u, 575u, 0xaa);

    message::filter_load instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter load  operator boolean not equals  differs  returns true", "[filter load]") {
    const message::filter_load expected(
        {0x0f, 0xf0, 0x55, 0xaa}, 643u, 575u, 0xaa);

    message::filter_load instance;
    REQUIRE(instance != expected);
}

// End Test Suite
