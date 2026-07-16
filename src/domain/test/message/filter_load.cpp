// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: filter load tests
TEST_CASE("filter load constructor 2 always equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load constructor 3 always equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    auto dup_filter = filter;
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(std::move(dup_filter), hash_functions, tweak, flags).value();
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load constructor 4 always equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const value = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    message::filter_load instance(value);
    REQUIRE(value == instance);
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load constructor 5 always equals params", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto value = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    message::filter_load instance(std::move(value));
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load from data insufficient bytes failure", "[filter load]") {
    data_chunk const raw{0xab, 0x11};
    message::filter_load instance;

    byte_reader reader(raw);
    auto result = message::filter_load::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("filter load from data insufficient version failure", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x05, 0xaa, 0xbb, 0xcc, 0xdd, 0xee},
        25,
        10,
        0xab
    ).value();

    data_chunk const data = kth::to_data_chunk(expected, message::version::level::maximum);
    
    byte_reader reader(data);
    auto result = message::filter_load::from_data(reader, message::filter_load::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("filter load from data valid input success", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x05, 0xaa, 0xbb, 0xcc, 0xdd, 0xee},
        25,
        10,
        0xab).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::filter_load::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::maximum));
    REQUIRE(expected.serialized_size(message::version::level::maximum) ==
                        result.serialized_size(message::version::level::maximum));
}

TEST_CASE("filter load filter accessor 1 always returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load filter accessor 2 always returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load filter is set at construction", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};

    message::filter_load const empty;
    REQUIRE(filter != empty.filter());

    auto const instance = message::filter_load::create(filter, 0u, 0u, 0x00).value();
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load filter is set at construction, by move", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    data_chunk dup = filter;

    message::filter_load const empty;
    REQUIRE(filter != empty.filter());

    auto const instance = message::filter_load::create(std::move(dup), 0u, 0u, 0x00).value();
    REQUIRE(filter == instance.filter());
}

TEST_CASE("filter load hash functions accessor always returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(hash_functions == instance.hash_functions());
}

TEST_CASE("filter load hash functions are set at construction", "[filter load]") {
    uint32_t hash_functions = 48u;

    message::filter_load const empty;
    REQUIRE(hash_functions != empty.hash_functions());

    auto const instance = message::filter_load::create({}, hash_functions, 0u, 0x00).value();
    REQUIRE(hash_functions == instance.hash_functions());
}

TEST_CASE("filter load tweak accessor always returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(tweak == instance.tweak());
}

TEST_CASE("filter load tweak is set at construction", "[filter load]") {
    uint32_t tweak = 36u;

    message::filter_load const empty;
    REQUIRE(tweak != empty.tweak());

    auto const instance = message::filter_load::create({}, 0u, tweak, 0x00).value();
    REQUIRE(tweak == instance.tweak());
}

TEST_CASE("filter load flags accessor always returns initialized value", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;

    auto const instance = message::filter_load::create(filter, hash_functions, tweak, flags).value();
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load flags are set at construction", "[filter load]") {
    uint8_t flags = 0xae;

    message::filter_load const empty;
    REQUIRE(flags != empty.flags());

    auto const instance = message::filter_load::create({}, 0u, 0u, flags).value();
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load operator assign equals always matches equivalent", "[filter load]") {
    data_chunk const filter = {0x0f, 0xf0, 0x55, 0xaa};
    uint32_t hash_functions = 48u;
    uint32_t tweak = 36u;
    uint8_t flags = 0xae;
    auto value = message::filter_load::create(filter, hash_functions, tweak, flags).value();


    message::filter_load instance;

    instance = std::move(value);
    REQUIRE(filter == instance.filter());
    REQUIRE(hash_functions == instance.hash_functions());
    REQUIRE(tweak == instance.tweak());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("filter load operator boolean equals duplicates returns true", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x0f, 0xf0, 0x55, 0xaa}, 43u, 575u, 0xaa).value();

    message::filter_load instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter load operator boolean equals differs returns false", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x0f, 0xf0, 0x55, 0xaa}, 43u, 575u, 0xaa).value();

    message::filter_load instance;
    REQUIRE(instance != expected);
}

TEST_CASE("filter load operator boolean not equals duplicates returns false", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x0f, 0xf0, 0x55, 0xaa}, 43u, 575u, 0xaa).value();

    message::filter_load instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter load operator boolean not equals differs returns true", "[filter load]") {
    auto const expected = message::filter_load::create(
        {0x0f, 0xf0, 0x55, 0xaa}, 43u, 575u, 0xaa).value();

    message::filter_load instance;
    REQUIRE(instance != expected);
}

TEST_CASE("filter load create rejects a filter over the BIP37 cap", "[filter load]") {
    auto const at_cap = message::filter_load::create(data_chunk(max_filter_load, 0x00), 1u, 0u, 0x00);
    REQUIRE(at_cap);

    auto const over = message::filter_load::create(data_chunk(max_filter_load + 1, 0x00), 1u, 0u, 0x00);
    REQUIRE( ! over);
    REQUIRE(over.error() == error::invalid_filter_load);
}

TEST_CASE("filter load create rejects too many hash functions", "[filter load]") {
    auto const at_cap = message::filter_load::create({0x00}, max_filter_functions, 0u, 0x00);
    REQUIRE(at_cap);

    auto const over = message::filter_load::create({0x00}, max_filter_functions + 1, 0u, 0x00);
    REQUIRE( ! over);
    REQUIRE(over.error() == error::invalid_filter_load);
}

// End Test Suite
