// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

namespace {

uint64_t convert_to_uint64t(std::string const& rawdata) {
    uint64_t value;
    std::istringstream iss(rawdata);
    iss >> value;
    return value;
}

chain::header const test_header{10u,
    "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
    "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
    531234u,
    6523454u,
    68644u};

message::compact_block::short_id_list const test_short_ids{
    convert_to_uint64t("aaaaaaaaaaaa"),
    convert_to_uint64t("bbbbbbbbbbbb"),
    convert_to_uint64t("0f0f0f0f0f0f"),
    convert_to_uint64t("f0f0f0f0f0f0")};

message::prefilled_transaction::list const test_transactions{
    message::prefilled_transaction::create(10, chain::transaction(1, 48, {}, {})).value(),
    message::prefilled_transaction::create(20, chain::transaction(2, 32, {}, {})).value(),
    message::prefilled_transaction::create(30, chain::transaction(4, 16, {}, {})).value()};

message::compact_block make_compact_block(uint64_t nonce = 453245u) {
    return message::compact_block(test_header, nonce, test_short_ids, test_transactions);
}

} // namespace

// Start Test Suite: compact block tests

TEST_CASE("compact block create always equals params", "[compact block]") {
    uint64_t const nonce = 453245u;
    auto const instance = message::compact_block(test_header, nonce, test_short_ids, test_transactions);
    REQUIRE(test_header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(test_short_ids == instance.short_ids());
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block copy equals params", "[compact block]") {
    auto const value = make_compact_block();
    message::compact_block instance(value);
    REQUIRE(value == instance);
    REQUIRE(test_header == instance.header());
    REQUIRE(453245u == instance.nonce());
    REQUIRE(test_short_ids == instance.short_ids());
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block from data insufficient bytes failure", "[compact block]") {
    data_chunk const raw{0xab, 0xcd};
    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

// TODO(fernando): review - original hex had 267 chars (odd), base16_literal truncated last char silently. Fixed to 266 chars.
TEST_CASE("compact block from data insufficient bytes mid transaction failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000"_base16);

    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data insufficient version failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = kth::to_data_chunk(expected, message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(raw);
    auto result = message::compact_block::from_data(reader2, message::compact_block::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data valid input success", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = kth::to_data_chunk(expected, message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(data);
    auto const result_exp2 = message::compact_block::from_data(reader2, message::compact_block::version_minimum);
    REQUIRE(result_exp2);
    auto const result = std::move(*result_exp2);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::compact_block::version_minimum));
    REQUIRE(expected.serialized_size(message::compact_block::version_minimum) == result.serialized_size(message::compact_block::version_minimum));
}

TEST_CASE("compact block header accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_header == instance.header());
}

TEST_CASE("compact block nonce accessor always returns initialized value", "[compact block]") {
    uint64_t const nonce = 453245u;
    auto const instance = make_compact_block(nonce);
    REQUIRE(nonce == instance.nonce());
}

TEST_CASE("compact block short ids accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_short_ids == instance.short_ids());
}

TEST_CASE("compact block transactions accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block operator boolean equals duplicates returns true", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    message::compact_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("compact block operator boolean equals differs returns false", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    auto const instance = message::compact_block(test_header, 99999u, test_short_ids, test_transactions);
    REQUIRE(instance != expected);
}

TEST_CASE("compact block operator boolean not equals duplicates returns false", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    message::compact_block instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("compact block operator boolean not equals differs returns true", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    auto const instance = message::compact_block(test_header, 99999u, test_short_ids, test_transactions);
    REQUIRE(instance != expected);
}

// End Test Suite
