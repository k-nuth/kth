// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: merkle block tests

TEST_CASE("merkle block  constructor 1  always invalid", "[merkle block]") {
    const message::merkle_block instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("merkle block  constructor 2  always  equals params", "[merkle block]") {
    chain::header const header(
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644);

    size_t const count = 1234u;

    hash_list const hashes{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    };
    data_chunk const flags{0xae, 0x56, 0x0f};

    message::merkle_block instance(header, count, hashes, flags);
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(instance.total_transactions() == count);
    REQUIRE(hashes == instance.hashes());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("merkle block  constructor 3  always  equals params", "[merkle block]") {
    const message::merkle_block instance(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        1234u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    REQUIRE(instance.is_valid());
}

TEST_CASE("merkle block  constructor 4  always  equals params", "[merkle block]") {
    const message::merkle_block expected(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        4321234u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    message::merkle_block instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("merkle block  constructor 5  always  equals params", "[merkle block]") {
    chain::header const header(
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644);

    size_t const count = 654576u;

    hash_list const hashes{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    };
    data_chunk const flags{0xae, 0x56, 0x0f};

    message::merkle_block expected(header, count, hashes, flags);
    message::merkle_block instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(instance.total_transactions() == count);
    REQUIRE(hashes == instance.hashes());
    REQUIRE(flags == instance.flags());
}

TEST_CASE("from data insufficient data fails", "[merkle block]") {
    data_chunk const data{10};
    message::merkle_block instance{};

    byte_reader reader(data);
    auto result = message::merkle_block::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("from data insufficient version fails", "[merkle block]") {
    const message::merkle_block expected{
        {10,
         hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
         hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
         531234,
         6523454,
         68644},
        34523u,
        {hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")},
        {0x00}};

    auto const data = expected.to_data(message::version::level::maximum);
    message::merkle_block instance{};

    byte_reader reader(data);
    auto result = message::merkle_block::from_data(reader, message::merkle_block::version_minimum - 1);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("merkle block - roundtrip to data factory from data chunk", "[merkle block]") {
    const message::merkle_block expected{
        {10,
         hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
         hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
         531234,
         6523454,
         68644},
        45633u,
        {hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")},
        {0x00}};

    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::merkle_block::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}



TEST_CASE("merkle block  header accessor 1  always  returns initialized value", "[merkle block]") {
    chain::header const expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    const message::merkle_block instance(
        expected,
        753u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    REQUIRE(expected == instance.header());
}

TEST_CASE("merkle block  header accessor 2  always  returns initialized value", "[merkle block]") {
    chain::header const expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    const message::merkle_block instance(
        expected,
        9542u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    REQUIRE(expected == instance.header());
}

TEST_CASE("merkle block  header setter 1  roundtrip  success", "[merkle block]") {
    chain::header const expected{
        10,
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
        531234,
        6523454,
        68644};

    message::merkle_block instance;
    REQUIRE(expected != instance.header());
    instance.set_header(expected);
    REQUIRE(expected == instance.header());
}

TEST_CASE("merkle block  header setter 2  roundtrip  success", "[merkle block]") {
    message::merkle_block instance;
    REQUIRE( ! instance.header().is_valid());
    instance.set_header(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644});

    REQUIRE(instance.header().is_valid());
}

TEST_CASE("merkle block  hashes accessor 1  always  returns initialized value", "[merkle block]") {
    hash_list const expected{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    };

    const message::merkle_block instance(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        2456u,
        expected,
        {0xae, 0x56, 0x0f});

    REQUIRE(expected == instance.hashes());
}

TEST_CASE("merkle block  hashes accessor 2  always  returns initialized value", "[merkle block]") {
    hash_list const expected{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    };

    const message::merkle_block instance(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        9137u,
        expected,
        {0xae, 0x56, 0x0f});

    REQUIRE(expected == instance.hashes());
}

TEST_CASE("merkle block  hashes setter 1  roundtrip  success", "[merkle block]") {
    hash_list const expected{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    };

    message::merkle_block instance;
    REQUIRE(expected != instance.hashes());
    instance.set_hashes(expected);
    REQUIRE(expected == instance.hashes());
}

TEST_CASE("merkle block  hashes setter 2  roundtrip  success", "[merkle block]") {
    message::merkle_block instance;
    REQUIRE(instance.hashes().empty());
    instance.set_hashes(hash_list{
        hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
        hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
    });

    REQUIRE( ! instance.hashes().empty());
}

TEST_CASE("merkle block  flags accessor 1  always  returns initialized value", "[merkle block]") {
    data_chunk const expected{0xae, 0x56, 0x0f};

    const message::merkle_block instance(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        8264u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        expected);

    REQUIRE(expected == instance.flags());
}

TEST_CASE("merkle block  flags accessor 2  always  returns initialized value", "[merkle block]") {
    data_chunk const expected{0xae, 0x56, 0x0f};

    const message::merkle_block instance(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        6428u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        expected);

    REQUIRE(expected == instance.flags());
}

TEST_CASE("merkle block  flags setter 1  roundtrip  success", "[merkle block]") {
    data_chunk const expected{0xae, 0x56, 0x0f};
    message::merkle_block instance;
    REQUIRE(expected != instance.flags());
    instance.set_flags(expected);
    REQUIRE(expected == instance.flags());
}

TEST_CASE("merkle block  flags setter 2  roundtrip  success", "[merkle block]") {
    message::merkle_block instance;
    REQUIRE(instance.flags().empty());
    instance.set_flags(data_chunk{0xae, 0x56, 0x0f});
    REQUIRE( ! instance.flags().empty());
}

TEST_CASE("merkle block  operator assign equals  always  matches equivalent", "[merkle block]") {
    message::merkle_block value(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        3197u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    REQUIRE(value.is_valid());

    message::merkle_block instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("merkle block  operator boolean equals  duplicates  returns true", "[merkle block]") {
    const message::merkle_block expected(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        9821u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    message::merkle_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("merkle block  operator boolean equals  differs  returns false", "[merkle block]") {
    const message::merkle_block expected(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        1469u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    message::merkle_block instance;
    REQUIRE( ! (instance == expected));
}

TEST_CASE("merkle block  operator boolean not equals  duplicates  returns false", "[merkle block]") {
    const message::merkle_block expected(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        3524u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    message::merkle_block instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("merkle block  operator boolean not equals  differs  returns true", "[merkle block]") {
    const message::merkle_block expected(
        chain::header{
            10,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234,
            6523454,
            68644},
        8642u,
        {
            hash_literal("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffffffffffffffffffffffffffff"),
            hash_literal("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            hash_literal("ccccccccccccccccccccccccccccccccdddddddddddddddddddddddddddddddd"),
        },
        {0xae, 0x56, 0x0f});

    message::merkle_block instance;
    REQUIRE(instance != expected);
}

// End Test Suite
