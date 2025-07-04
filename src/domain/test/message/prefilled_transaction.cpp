// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: prefilled transaction tests

TEST_CASE("prefilled transaction  constructor 1  always invalid", "[prefilled transaction]") {
    message::prefilled_transaction instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("prefilled transaction  constructor 2  always  equals params", "[prefilled transaction]") {
    uint64_t index = 125u;
    chain::transaction tx(1, 0, {}, {});
    message::prefilled_transaction instance(index, tx);
    REQUIRE(instance.is_valid());
    REQUIRE(index == instance.index());
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction  constructor 3  always  equals params", "[prefilled transaction]") {
    uint64_t index = 125u;
    chain::transaction tx(1, 0, {}, {});
    REQUIRE(tx.is_valid());
    message::prefilled_transaction instance(index, std::move(tx));
    REQUIRE(instance.is_valid());
    REQUIRE(index == instance.index());
    REQUIRE(instance.transaction().is_valid());
}

TEST_CASE("prefilled transaction  constructor 4  always  equals params", "[prefilled transaction]") {
    const message::prefilled_transaction expected(125u,
                                                  chain::transaction{1, 0, {}, {}});

    message::prefilled_transaction instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("prefilled transaction  constructor 5  always  equals params", "[prefilled transaction]") {
    message::prefilled_transaction expected(125u,
                                            chain::transaction{1, 0, {}, {}});

    message::prefilled_transaction instance(std::move(expected));
    REQUIRE(instance.is_valid());
}

TEST_CASE("prefilled transaction from data insufficient bytes  failure", "[prefilled transaction]") {
    data_chunk const raw{1};
    message::prefilled_transaction instance{};
    byte_reader reader(raw);
    auto result = message::prefilled_transaction::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("prefilled transaction from data valid input  success", "[prefilled transaction]") {
    const message::prefilled_transaction expected(
        16,
        chain::transaction{
            1,
            0,
            {},
            {}});

    auto const data = expected.to_data(message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::prefilled_transaction::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}



TEST_CASE("prefilled transaction  index accessor  always  returns initialized value", "[prefilled transaction]") {
    uint64_t index = 634u;
    chain::transaction tx(5, 23, {}, {});
    message::prefilled_transaction instance(index, tx);
    REQUIRE(index == instance.index());
}

TEST_CASE("prefilled transaction  index setter  roundtrip  success", "[prefilled transaction]") {
    uint64_t index = 634u;
    message::prefilled_transaction instance;
    REQUIRE(index != instance.index());
    instance.set_index(index);
    REQUIRE(index == instance.index());
}

TEST_CASE("prefilled transaction  message accessor 1  always  returns initialized value", "[prefilled transaction]") {
    uint64_t index = 634u;
    chain::transaction const tx(5, 23, {}, {});
    message::prefilled_transaction instance(index, tx);
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction  message accessor 2  always  returns initialized value", "[prefilled transaction]") {
    uint64_t index = 634u;
    chain::transaction const tx(5, 23, {}, {});
    const message::prefilled_transaction instance(index, tx);
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction  message setter 1  roundtrip  success", "[prefilled transaction]") {
    chain::transaction const tx(5, 23, {}, {});
    message::prefilled_transaction instance;
    REQUIRE(tx != instance.transaction());
    instance.set_transaction(tx);
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction  message setter 2  roundtrip  success", "[prefilled transaction]") {
    chain::transaction const duplicate(16, 57, {}, {});
    chain::transaction tx(16, 57, {}, {});
    message::prefilled_transaction instance;
    REQUIRE(duplicate != instance.transaction());
    instance.set_transaction(std::move(tx));
    REQUIRE(duplicate == instance.transaction());
}

TEST_CASE("prefilled transaction  operator assign equals 1  always  matches equivalent", "[prefilled transaction]") {
    message::prefilled_transaction value(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    REQUIRE(value.is_valid());

    message::prefilled_transaction instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("prefilled transaction  operator assign equals 2  always  matches equivalent", "[prefilled transaction]") {
    const message::prefilled_transaction value(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    REQUIRE(value.is_valid());

    message::prefilled_transaction instance;
    REQUIRE( ! instance.is_valid());

    instance = value;
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("prefilled transaction  operator boolean equals  duplicates  returns true", "[prefilled transaction]") {
    const message::prefilled_transaction expected(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    message::prefilled_transaction instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("prefilled transaction  operator boolean equals  differs  returns false", "[prefilled transaction]") {
    const message::prefilled_transaction expected(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    message::prefilled_transaction instance;
    REQUIRE(instance != expected);
}

TEST_CASE("prefilled transaction  operator boolean not equals  duplicates  returns false", "[prefilled transaction]") {
    const message::prefilled_transaction expected(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    message::prefilled_transaction instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("prefilled transaction  operator boolean not equals  differs  returns true", "[prefilled transaction]") {
    const message::prefilled_transaction expected(
        1234u,
        chain::transaction{6u, 10u, {}, {}});

    message::prefilled_transaction instance;
    REQUIRE(instance != expected);
}

// End Test Suite
