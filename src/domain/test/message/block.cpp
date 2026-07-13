// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure/utility/ostream_writer.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

namespace {
// Valid blocks (non-zero header, no transactions) for fixtures that only need
// "some block" — `create` rejects the empty sentinel.
chain::block make_chain_block() {
    return chain::block::create(chain::header{1u, null_hash, null_hash, 0u, 0u, 0u}, {}).value();
}
message::block make_msg_block() {
    return message::block::create(chain::header{1u, null_hash, null_hash, 0u, 0u, 0u}, {}).value();
}
} // anonymous namespace

// Start Test Suite: message block tests

TEST_CASE("block create rejects the empty sentinel", "[message block]") {
    REQUIRE( ! block::create(chain::header{}, {}));
}

TEST_CASE("block constructor 2 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto instance = block::create(header, transactions).value();
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 3 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::header dup_header(header);
    chain::transaction::list dup_transactions = transactions;
    auto instance = block::create(std::move(dup_header), std::move(dup_transactions)).value();
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 4 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = chain::block::create(header, transactions).value();
    block instance(value);
    REQUIRE(instance == value);
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 5 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = chain::block::create(header, transactions).value();
    block instance(std::move(value));
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 6 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = block::create(header, transactions).value();
    block instance(value);
    REQUIRE(value == instance);
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 7 always equals params", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = block::create(header, transactions).value();
    block instance(std::move(value));
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block factory data 1 genesis mainnet success", "[message block]") {
    auto const genesis = chain::block::genesis_mainnet();
    REQUIRE(genesis.serialized_size() == 285u);
    REQUIRE(genesis.header().serialized_size() == 80u);

    // Save genesis block.
    auto raw_block = kth::to_data_chunk(genesis);
    REQUIRE(raw_block.size() == 285u);

    // Reload genesis block.
    byte_reader reader(raw_block);
    auto const result_exp = message::block::from_data(reader, version::level::minimum);
    REQUIRE(result_exp);
    auto const block = std::move(*result_exp);

    REQUIRE(genesis.header() == block.header());

    // Verify merkle root from transactions.
    REQUIRE(block.generate_merkle_root() == genesis.header().merkle());

    auto raw_reserialization = kth::to_data_chunk(block, version::level::minimum);
    REQUIRE(raw_reserialization == raw_block);
    REQUIRE(raw_reserialization.size() == block.serialized_size(version::level::minimum));
}



TEST_CASE("block operator assign equals 1 always matches equivalent", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = chain::block::create(header, transactions).value();

    auto instance = make_msg_block();

    instance = std::move(value);
    REQUIRE(instance.header() == header);
    REQUIRE(instance.transactions() == transactions);
}

TEST_CASE("block operator assign equals 2 always matches equivalent", "[message block]") {
    chain::header const header(10u,
                               "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                               "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    auto value = message::block::create(header, transactions).value();


    auto instance = make_msg_block();

    instance = std::move(value);
    REQUIRE(instance.header() == header);
    REQUIRE(instance.transactions() == transactions);
}

TEST_CASE("block operator boolean equals 1 duplicates returns true", "[message block]") {
    auto const expected = chain::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    message::block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block operator boolean equals 1 differs returns false", "[message block]") {
    auto const expected = chain::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    auto instance = make_msg_block();
    REQUIRE(instance != expected);
}

TEST_CASE("block operator boolean not equals 1 duplicates returns false", "[message block]") {
    auto const expected = chain::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    message::block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block operator boolean not equals 1 differs returns true", "[message block]") {
    auto const expected = chain::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    auto instance = make_chain_block();
    REQUIRE(instance != expected);
}

TEST_CASE("block operator boolean equals 2 duplicates returns true", "[message block]") {
    auto const expected = message::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    message::block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block operator boolean equals 2 differs returns false", "[message block]") {
    auto const expected = message::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    auto instance = make_msg_block();
    REQUIRE(instance != expected);
}

TEST_CASE("block operator boolean not equals 2 duplicates returns false", "[message block]") {
    auto const expected = message::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    message::block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block operator boolean not equals 2 differs returns true", "[message block]") {
    auto const expected = message::block::create(
        chain::header(10u,
                      "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
                      "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})}).value();

    auto instance = make_msg_block();
    REQUIRE(instance != expected);
}

// End Test Suite
