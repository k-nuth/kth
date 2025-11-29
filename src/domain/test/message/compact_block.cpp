// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: compact block tests

TEST_CASE("compact block  constructor 1  always invalid", "[compact block]") {
    message::compact_block instance;
    REQUIRE( ! instance.is_valid());
}

uint64_t convert_to_uint64t(std::string const& rawdata) {
    uint64_t value;
    std::istringstream iss(rawdata);
    iss >> value;
    return value;
}

TEST_CASE("compact block  constructor 2  always  equals params", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;

    const message::compact_block::short_id_list& short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(short_ids == instance.short_ids());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  constructor 3  always  equals params", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);
    chain::header dup_header(header);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};
    message::compact_block::short_id_list dup_short_ids = short_ids;

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};
    message::prefilled_transaction::list dup_transactions = transactions;

    message::compact_block instance(std::move(dup_header), nonce,
                                    std::move(dup_short_ids), std::move(dup_transactions));

    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(short_ids == instance.short_ids());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  constructor 4  always  equals params", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    const message::compact_block value(header, nonce, short_ids, transactions);

    message::compact_block instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(short_ids == instance.short_ids());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  constructor 5  always  equals params", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block value(header, nonce, short_ids, transactions);

    message::compact_block instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(short_ids == instance.short_ids());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block from data insufficient bytes  failure", "[compact block]") {
    data_chunk const raw{0xab, 0xcd};
    message::compact_block instance{};
    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

// TODO(fernando): review - original hex had 267 chars (odd), base16_literal truncated last char silently. Fixed to 266 chars.
TEST_CASE("compact block from data insufficient bytes mid transaction  failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000"_base16);

    message::compact_block instance{};
    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data insufficient version  failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = expected.to_data(message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(raw);
    auto result = message::compact_block::from_data(reader2, message::compact_block::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data valid input  success", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = expected.to_data(message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(data);
    auto const result_exp2 = message::compact_block::from_data(reader2, message::compact_block::version_minimum);
    REQUIRE(result_exp2);
    auto const result = std::move(*result_exp2);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::compact_block::version_minimum));
    REQUIRE(expected.serialized_size(message::compact_block::version_minimum) == result.serialized_size(message::compact_block::version_minimum));
}

TEST_CASE("compact block  header accessor 1  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(header == instance.header());
}

TEST_CASE("compact block  header accessor 2  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    const message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(header == instance.header());
}

TEST_CASE("compact block  header setter 1  roundtrip  success", "[compact block]") {
    chain::header const value(10u,
                              hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                              hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                              531234u,
                              6523454u,
                              68644u);

    message::compact_block instance;
    REQUIRE(value != instance.header());
    instance.set_header(value);
    REQUIRE(value == instance.header());
}

TEST_CASE("compact block  header setter 2  roundtrip  success", "[compact block]") {
    chain::header const value(10u,
                              hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                              hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                              531234u,
                              6523454u,
                              68644u);
    chain::header dup(value);

    message::compact_block instance;
    REQUIRE(value != instance.header());
    instance.set_header(std::move(dup));
    REQUIRE(value == instance.header());
}

TEST_CASE("compact block  nonce accessor  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(nonce == instance.nonce());
}

TEST_CASE("compact block  nonce setter  roundtrip  success", "[compact block]") {
    uint64_t value = 123356u;

    message::compact_block instance;
    REQUIRE(value != instance.nonce());
    instance.set_nonce(value);
    REQUIRE(value == instance.nonce());
}

TEST_CASE("compact block  short ids accessor 1  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(short_ids == instance.short_ids());
}

TEST_CASE("compact block  short ids accessor 2  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    const message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(short_ids == instance.short_ids());
}

TEST_CASE("compact block  short ids setter 1  roundtrip  success", "[compact block]") {
    const message::compact_block::short_id_list value = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    message::compact_block instance;
    REQUIRE(value != instance.short_ids());
    instance.set_short_ids(value);
    REQUIRE(value == instance.short_ids());
}

TEST_CASE("compact block  short ids setter 2  roundtrip  success", "[compact block]") {
    const message::compact_block::short_id_list value = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};
    message::compact_block::short_id_list dup(value);

    message::compact_block instance;
    REQUIRE(value != instance.short_ids());
    instance.set_short_ids(std::move(dup));
    REQUIRE(value == instance.short_ids());
}

TEST_CASE("compact block  transactions accessor 1  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  transactions accessor 2  always  returns initialized value", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    const message::compact_block instance(header, nonce, short_ids, transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  transactions setter 1  roundtrip  success", "[compact block]") {
    const message::prefilled_transaction::list value = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block instance;
    REQUIRE(value != instance.transactions());
    instance.set_transactions(value);
    REQUIRE(value == instance.transactions());
}

TEST_CASE("compact block  transactions setter 2  roundtrip  success", "[compact block]") {
    const message::prefilled_transaction::list value = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};
    message::prefilled_transaction::list dup(value);

    message::compact_block instance;
    REQUIRE(value != instance.transactions());
    instance.set_transactions(std::move(dup));
    REQUIRE(value == instance.transactions());
}

TEST_CASE("compact block  operator assign equals  always  matches equivalent", "[compact block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    uint64_t nonce = 453245u;
    const message::compact_block::short_id_list short_ids = {
        convert_to_uint64t("aaaaaaaaaaaa"),
        convert_to_uint64t("bbbbbbbbbbbb"),
        convert_to_uint64t("0f0f0f0f0f0f"),
        convert_to_uint64t("f0f0f0f0f0f0")};

    const message::prefilled_transaction::list transactions = {
        message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
        message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
        message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))};

    message::compact_block value(header, nonce, short_ids, transactions);
    REQUIRE(value.is_valid());
    message::compact_block instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(short_ids == instance.short_ids());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("compact block  operator boolean equals  duplicates  returns true", "[compact block]") {
    const message::compact_block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        12334u,
        {convert_to_uint64t("aaaaaaaaaaaa"),
         convert_to_uint64t("bbbbbbbbbbbb"),
         convert_to_uint64t("0f0f0f0f0f0f"),
         convert_to_uint64t("f0f0f0f0f0f0")},
        {message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
         message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
         message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))});

    message::compact_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("compact block  operator boolean equals  differs  returns false", "[compact block]") {
    const message::compact_block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        12334u,
        {convert_to_uint64t("aaaaaaaaaaaa"),
         convert_to_uint64t("bbbbbbbbbbbb"),
         convert_to_uint64t("0f0f0f0f0f0f"),
         convert_to_uint64t("f0f0f0f0f0f0")},
        {message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
         message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
         message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))});

    message::compact_block instance;
    REQUIRE(instance != expected);
}

TEST_CASE("compact block  operator boolean not equals  duplicates  returns false", "[compact block]") {
    const message::compact_block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        12334u,
        {convert_to_uint64t("aaaaaaaaaaaa"),
         convert_to_uint64t("bbbbbbbbbbbb"),
         convert_to_uint64t("0f0f0f0f0f0f"),
         convert_to_uint64t("f0f0f0f0f0f0")},
        {message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
         message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
         message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))});

    message::compact_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("compact block  operator boolean not equals  differs  returns true", "[compact block]") {
    const message::compact_block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        12334u,
        {convert_to_uint64t("aaaaaaaaaaaa"),
         convert_to_uint64t("bbbbbbbbbbbb"),
         convert_to_uint64t("0f0f0f0f0f0f"),
         convert_to_uint64t("f0f0f0f0f0f0")},
        {message::prefilled_transaction(10, chain::transaction(1, 48, {}, {})),
         message::prefilled_transaction(20, chain::transaction(2, 32, {}, {})),
         message::prefilled_transaction(30, chain::transaction(4, 16, {}, {}))});

    message::compact_block instance;
    REQUIRE(instance != expected);
}

// End Test Suite
