// Copyright (c) 2016-present Knuth Project developers.
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
TEST_CASE("address constructor 2 always equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto const instance = address::create(addresses).value();

    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address constructor 3 always equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto dup_addresses = addresses;

    auto const instance = address::create(std::move(dup_addresses)).value();

    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address constructor 4 always equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto value = address::create(addresses).value();
    address instance(value);

    REQUIRE(value == instance);
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address constructor 5 always equals params", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto value = address::create(addresses).value();
    address instance(std::move(value));

    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address from data insufficient bytes failure", "[address]") {
    data_chunk const raw{0xab};
    address instance;

    byte_reader reader(raw);
    auto result = address::from_data(reader, version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("address from data roundtrip success", "[address]") {
    auto const expected = address::create(
        {{734678u,
          5357534u,
          "47816a40bb92bdb4e0b8256861f96a55"_base16,
          123u}}).value();

    auto const data = kth::to_data_chunk(expected, version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = address::from_data(reader, version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(equal(expected, result));
    auto const serialized_size = result.serialized_size(version::level::minimum);
    REQUIRE(data.size() == serialized_size);
    REQUIRE(expected.serialized_size(version::level::minimum) == serialized_size);
}

TEST_CASE("address addresses are set at construction", "[address]") {
    infrastructure::message::network_address::list const value{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    address const empty;
    REQUIRE(empty.addresses() != value);

    auto const instance = address::create(value).value();
    REQUIRE(value == instance.addresses());
}

TEST_CASE("address addresses are set at construction, by move", "[address]") {
    infrastructure::message::network_address::list const value{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto dup_value = value;
    address const empty;
    REQUIRE(empty.addresses() != value);

    auto const instance = address::create(std::move(dup_value)).value();
    REQUIRE(value == instance.addresses());
}

TEST_CASE("address operator assign equals always matches equivalent", "[address]") {
    infrastructure::message::network_address::list const addresses{
        network_address(
            734678u,
            5357534u,
            "47816a40bb92bdb4e0b8256861f96a55"_base16,
            123u),
        network_address(
            34654u,
            47653u,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
            222u),
        network_address(
            265453u,
            2115325u,
            "19573257168426842319857321595126"_base16,
            159u)};

    auto value = address::create(addresses).value();


    address instance;

    instance = std::move(value);
    REQUIRE(addresses == instance.addresses());
}

TEST_CASE("address operator boolean equals duplicates returns true", "[address]") {
    auto const expected = address::create(
        {network_address(
             734678u,
             5357534u,
             "47816a40bb92bdb4e0b8256861f96a55"_base16,
             123u),
         network_address(
             34654u,
             47653u,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
             222u),
         network_address(
             265453u,
             2115325u,
             "19573257168426842319857321595126"_base16,
             159u)}).value();

    address instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("address operator boolean equals differs returns false", "[address]") {
    auto const expected = address::create(
        {network_address(
             734678u,
             5357534u,
             "47816a40bb92bdb4e0b8256861f96a55"_base16,
             123u),
         network_address(
             34654u,
             47653u,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
             222u),
         network_address(
             265453u,
             2115325u,
             "19573257168426842319857321595126"_base16,
             159u)}).value();

    address instance;
    REQUIRE(instance != expected);
}

TEST_CASE("address operator boolean not equals duplicates returns false", "[address]") {
    auto const expected = address::create(
        {network_address(
             734678u,
             5357534u,
             "47816a40bb92bdb4e0b8256861f96a55"_base16,
             123u),
         network_address(
             34654u,
             47653u,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
             222u),
         network_address(
             265453u,
             2115325u,
             "19573257168426842319857321595126"_base16,
             159u)}).value();

    address instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("address operator boolean not equals differs returns true", "[address]") {
    auto const expected = address::create(
        {network_address(
             734678u,
             5357534u,
             "47816a40bb92bdb4e0b8256861f96a55"_base16,
             123u),
         network_address(
             34654u,
             47653u,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_base16,
             222u),
         network_address(
             265453u,
             2115325u,
             "19573257168426842319857321595126"_base16,
             159u)}).value();

    address instance;
    REQUIRE(instance != expected);
}

TEST_CASE("address create rejects more entries than the protocol allows", "[address]") {
    infrastructure::message::network_address::list at_cap(max_address);
    REQUIRE(address::create(at_cap));

    infrastructure::message::network_address::list over(max_address + 1);
    auto const result = address::create(std::move(over));
    REQUIRE( ! result);
    REQUIRE(result.error() == error::invalid_address_count);
}

// End Test Suite
