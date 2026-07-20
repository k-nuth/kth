// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

namespace {

// BIP152 caps the prefilled index; mirrors the domain's own limit.
#if defined(KTH_CURRENCY_BCH)
constexpr uint64_t max_index = max_uint32;
#else
constexpr uint64_t max_index = max_uint16;
#endif

message::prefilled_transaction make_prefilled(uint64_t index = 1234u) {
    return message::prefilled_transaction::create(index, chain::transaction{6u, 10u, {}, {}}).value();
}

} // namespace

// Start Test Suite: prefilled transaction tests

TEST_CASE("prefilled transaction create rejects an out of range index", "[prefilled transaction]") {
    auto const result = message::prefilled_transaction::create(max_index, chain::transaction{1, 0, {}, {}});
    REQUIRE( ! result);
    REQUIRE(result.error() == error::invalid_compact_block);
}

TEST_CASE("prefilled transaction create accepts the last in range index", "[prefilled transaction]") {
    auto const result = message::prefilled_transaction::create(max_index - 1u, chain::transaction{1, 0, {}, {}});
    REQUIRE(result);
    REQUIRE(result->index() == max_index - 1u);
}

TEST_CASE("prefilled transaction create always equals params", "[prefilled transaction]") {
    uint64_t const index = 125u;
    chain::transaction const tx(1, 0, {}, {});
    auto const instance = message::prefilled_transaction::create(index, tx).value();
    REQUIRE(index == instance.index());
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction copy always equals params", "[prefilled transaction]") {
    auto const expected = make_prefilled(125u);
    message::prefilled_transaction instance(expected);
    REQUIRE(expected == instance);
}

TEST_CASE("prefilled transaction move always equals params", "[prefilled transaction]") {
    auto expected = make_prefilled(125u);
    auto const copy = expected;
    message::prefilled_transaction instance(std::move(expected));
    REQUIRE(copy == instance);
}

TEST_CASE("prefilled transaction from data insufficient bytes failure", "[prefilled transaction]") {
    data_chunk const raw{1};
    byte_reader reader(raw);
    auto result = message::prefilled_transaction::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("prefilled transaction from data valid input success", "[prefilled transaction]") {
    auto const expected = message::prefilled_transaction::create(
        16, chain::transaction{1, 0, {}, {}}).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::prefilled_transaction::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
}

TEST_CASE("prefilled transaction index accessor always returns initialized value", "[prefilled transaction]") {
    uint64_t const index = 634u;
    auto const instance = message::prefilled_transaction::create(index, chain::transaction{5, 23, {}, {}}).value();
    REQUIRE(index == instance.index());
}

TEST_CASE("prefilled transaction transaction accessor always returns initialized value", "[prefilled transaction]") {
    chain::transaction const tx(5, 23, {}, {});
    auto const instance = message::prefilled_transaction::create(634u, tx).value();
    REQUIRE(tx == instance.transaction());
}

TEST_CASE("prefilled transaction operator assign always matches equivalent", "[prefilled transaction]") {
    auto value = make_prefilled();
    auto const copy = value;
    auto instance = make_prefilled(1u);

    instance = std::move(value);
    REQUIRE(copy == instance);
}

TEST_CASE("prefilled transaction operator boolean equals duplicates returns true", "[prefilled transaction]") {
    auto const expected = make_prefilled();
    message::prefilled_transaction instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("prefilled transaction operator boolean equals differs returns false", "[prefilled transaction]") {
    auto const expected = make_prefilled();
    auto const instance = make_prefilled(4321u);
    REQUIRE( ! (instance == expected));
}

TEST_CASE("prefilled transaction operator boolean not equals duplicates returns false", "[prefilled transaction]") {
    auto const expected = make_prefilled();
    message::prefilled_transaction instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("prefilled transaction operator boolean not equals differs returns true", "[prefilled transaction]") {
    auto const expected = make_prefilled();
    auto const instance = make_prefilled(4321u);
    REQUIRE(instance != expected);
}

// End Test Suite
