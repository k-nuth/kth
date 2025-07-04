// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

bool equal(address const& x, address const& y) {
    auto const left_addresses = x.addresses();
    auto const right_addresses = y.addresses();

    bool same = (left_addresses.size() == right_addresses.size());

    for (size_t i = 0; (i < left_addresses.size()) && same; i++) {
        same = (left_addresses[i] == right_addresses[i]) && (left_addresses[i].timestamp() == right_addresses[i].timestamp());
    }

    return same;
}

// Start Test Suite: address tests

TEST_CASE("address  constructor 1  always invalid", "[address]") {
    address instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("address  constructor 2  always  equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    address instance(addresses);

    REQUIRE(instance.is_valid());
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address  constructor 3  always  equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    auto dup_addresses = addresses;

    address instance(std::move(dup_addresses));

    REQUIRE(instance.is_valid());
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address  constructor 4  always  equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    address value(addresses);
    address instance(value);

    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address  constructor 5  always  equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    address value(addresses);
    address instance(std::move(value));

    REQUIRE(instance.is_valid());
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address from data insufficient bytes  failure", "[address]") {
    data_chunk const raw{0xab};
    address instance;

    byte_reader reader(raw);
    auto result = address::from_data(reader, version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("address from data roundtrip  success", "[address]") {
    address const expected(
        {{734678u,
          5357534u,
          base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
          123u}});

    auto const data = expected.to_data(version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = address::from_data(reader, version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(equal(expected, result));
    auto const serialized_size = result.serialized_size(version::level::minimum);
    REQUIRE(data.size() == serialized_size);
    REQUIRE(expected.serialized_size(version::level::minimum) == serialized_size);
}



TEST_CASE("address  addresses setter 1  roundtrip  success", "[address]") {
    infrastructure::message::network_address::list const value{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    address instance;
    REQUIRE(instance.addresses() != value);
    instance.set_addresses(value);
    REQUIRE(value == instance.addresses());
}

TEST_CASE("address  addresses setter 2  roundtrip  success", "[address]") {
    infrastructure::message::network_address::list const value{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    auto dup_value = value;
    address instance;
    REQUIRE(instance.addresses() != value);
    instance.set_addresses(std::move(dup_value));
    REQUIRE(value == instance.addresses());
}

TEST_CASE("address  operator assign equals  always  matches equivalent", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
            123u),
        network_address(
            34654u,
            47653u,
            base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            222u),
        network_address(
            265453u,
            2115325u,
            base16_literal("19573257168426842319857321595126"),
            159u)};

    address value(addresses);

    REQUIRE(value.is_valid());

    address instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address  operator boolean equals  duplicates  returns true", "[address]") {
    address const expected(
        {network_address(
             734678u,
             5357534u,
             base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
             123u),
         network_address(
             34654u,
             47653u,
             base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
             222u),
         network_address(
             265453u,
             2115325u,
             base16_literal("19573257168426842319857321595126"),
             159u)});

    address instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("address  operator boolean equals  differs  returns false", "[address]") {
    address const expected(
        {network_address(
             734678u,
             5357534u,
             base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
             123u),
         network_address(
             34654u,
             47653u,
             base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
             222u),
         network_address(
             265453u,
             2115325u,
             base16_literal("19573257168426842319857321595126"),
             159u)});

    address instance;
    REQUIRE(instance != expected);
}

TEST_CASE("address  operator boolean not equals  duplicates  returns false", "[address]") {
    address const expected(
        {network_address(
             734678u,
             5357534u,
             base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
             123u),
         network_address(
             34654u,
             47653u,
             base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
             222u),
         network_address(
             265453u,
             2115325u,
             base16_literal("19573257168426842319857321595126"),
             159u)});

    address instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("address  operator boolean not equals  differs  returns true", "[address]") {
    address const expected(
        {network_address(
             734678u,
             5357534u,
             base16_literal("47816a40bb92bdb4e0b8256861f96a55"),
             123u),
         network_address(
             34654u,
             47653u,
             base16_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
             222u),
         network_address(
             265453u,
             2115325u,
             base16_literal("19573257168426842319857321595126"),
             159u)});

    address instance;
    REQUIRE(instance != expected);
}

// End Test Suite
