// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

namespace {

chain::header const test_header{
    10,
    "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
    "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
    531234,
    6523454,
    68644};

hash_list const test_hashes{
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"_hash,
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"_hash,
    "ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"_hash,
};

data_chunk const test_flags{0xae, 0x56, 0x0f};

message::merkle_block make_merkle_block(size_t count = 1234u) {
    return message::merkle_block::create(test_header, count, test_hashes, test_flags);
}

} // namespace

// Start Test Suite: merkle block tests

TEST_CASE("merkle block create always equals params", "[merkle block]") {
    size_t const count = 1234u;
    auto const instance = message::merkle_block::create(test_header, count, test_hashes, test_flags);
    REQUIRE(test_header == instance.header());
    REQUIRE(instance.total_transactions() == count);
    REQUIRE(test_hashes == instance.hashes());
    REQUIRE(test_flags == instance.flags());
}

TEST_CASE("merkle block wrap block equals params", "[merkle block]") {
    auto const expected = make_merkle_block(4321234u);
    message::merkle_block instance(expected);
    REQUIRE(expected == instance);
}

TEST_CASE("from data insufficient data fails", "[merkle block]") {
    data_chunk const data{10};

    byte_reader reader(data);
    auto result = message::merkle_block::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("from data insufficient version fails", "[merkle block]") {
    auto const expected = message::merkle_block::create(
        test_header,
        34523u,
        {"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash},
        {0x00});

    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);

    byte_reader reader(data);
    auto result = message::merkle_block::from_data(reader, message::merkle_block::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("merkle block - roundtrip to data factory from data chunk", "[merkle block]") {
    auto const expected = message::merkle_block::create(
        test_header,
        45633u,
        {"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash},
        {0x00});

    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::merkle_block::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
}

TEST_CASE("merkle block header accessor always returns initialized value", "[merkle block]") {
    auto const instance = make_merkle_block(753u);
    REQUIRE(test_header == instance.header());
}

TEST_CASE("merkle block hashes accessor always returns initialized value", "[merkle block]") {
    auto const instance = make_merkle_block(2456u);
    REQUIRE(test_hashes == instance.hashes());
}

TEST_CASE("merkle block flags accessor always returns initialized value", "[merkle block]") {
    auto const instance = make_merkle_block(8264u);
    REQUIRE(test_flags == instance.flags());
}

TEST_CASE("merkle block operator boolean equals duplicates returns true", "[merkle block]") {
    auto const expected = make_merkle_block(9821u);
    message::merkle_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("merkle block operator boolean equals differs returns false", "[merkle block]") {
    auto const expected = make_merkle_block(1469u);
    auto const instance = message::merkle_block::create(test_header, 1470u, {}, test_flags);
    REQUIRE( ! (instance == expected));
}

TEST_CASE("merkle block operator boolean not equals duplicates returns false", "[merkle block]") {
    auto const expected = make_merkle_block(3524u);
    message::merkle_block instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("merkle block operator boolean not equals differs returns true", "[merkle block]") {
    auto const expected = make_merkle_block(8642u);
    auto const instance = message::merkle_block::create(test_header, 8642u, {}, test_flags);
    REQUIRE(instance != expected);
}

// End Test Suite
