// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: block transactions tests

TEST_CASE("block transactions  constructor 1  always invalid", "[block transactions]") {
    message::block_transactions instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("block transactions  constructor 2  always  equals params", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions instance(hash, transactions);
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  constructor 3  always  equals params", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
    hash_digest dup_hash = hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};
    chain::transaction::list dup_transactions = transactions;

    message::block_transactions instance(std::move(dup_hash),
                                         std::move(dup_transactions));

    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  constructor 4  always  equals params", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions value(hash, transactions);
    message::block_transactions instance(value);

    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(hash == instance.block_hash());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  constructor 5  always  equals params", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions value(hash, transactions);
    message::block_transactions instance(std::move(value));

    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions from data insufficient bytes  failure", "[block transactions]") {
    data_chunk const raw{0xab, 0xcd};
    message::block_transactions instance{};

    byte_reader reader(raw);
    auto result = message::block_transactions::from_data(reader, message::block_transactions::version_minimum);
    REQUIRE( ! result);
}

// TODO(fernando): review - original hex had odd length, base16_literal truncated last char silently. Fixed to even length.
TEST_CASE("block transactions from data insufficient transaction bytes  failure", "[block transactions]") {
    data_chunk raw = to_chunk(
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "020100000001f08e44a96bfb5ae63eda1a6620adae37ee37ee4777fb0336e1bb"
        "bc4de65310fc010000006a473044022050d8368cacf9bf1b8fb1f7cfd9aff632"
        "94789eb1760139e7ef41f083726dadc4022067796354aba8f2e02363c5e510aa"
        "7e2830b115472fb31de67d16972867f13945012103e589480b2f746381fca01a"
        "9b12c517b7a482a203c8b2742985da0ac72cc078f2ffffffff02f0c9c4670000"
        "00001976a914d9d78e26df4e4601cf9b26d09c7b280ee764469f88ac80c4600f"
        "000000001976a9141ee32412020a324b93b1a1acfdfff6ab9ca8fac288ac0000"
        "0000010000000364e62ad837f29617bafeae951776e7a6b3019b2da378279215"
        "48d1a5efcf9e5c010000006b48304502204df0dc9b7f61fbb2e4c8b0e09f3426"
        "d625a0191e56c48c338df3214555180eaf022100f21ac1f632201154f3c69e1e"
        "adb59901a34c40f1127e96adc31fac6ae6b11fb4012103893d5a06201d5cf614"
        "00e96fa4a7514fc12ab45166ace618d68b8066c9c585f9ffffffff54b755c392"
        "07d443fd96a8d12c94446a1c6f66e39c95e894c23418d7501f681b010000006b"
        "48304502203267910f55f2297360198fff57a3631be850965344370f732950b4"
        "7795737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d1"
        "5d915da450151d36012103893d5a06201d5cf61400e96fa4a7514fc12ab45166"
        "ace618d68b8066c9c585f9ffffffff0aa14d394a1f0eaf0c4496537f8ab9246d"
        "9663e26acb5f308fccc734b748cc9c010000006c493046022100d64ace8ec2d5"
        "feeb3e868e82b894202db8cb683c414d806b343d02b7ac679de7022100a2dcd3"
        "9940dd28d4e22cce417a0829c1b516c471a3d64d11f2c5d754108bdc0b012103"
        "893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9"
        "ffffffff02c0e1e400000000001976a914884c09d7e1f6420976c40e040c30b2"
        "b62210c3d488ac20300500000000001976a914905f933de850988603aafeeb2f"
        "d7fce61e66fe5d88ac0000"_base16);  // truncated for "insufficient bytes" test

    message::block_transactions instance{};

    byte_reader reader(raw);
    auto result = message::block_transactions::from_data(reader, message::block_transactions::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("block transactions from data insufficient version  failure", "[block transactions]") {
    data_chunk raw = to_chunk(
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "020100000001f08e44a96bfb5ae63eda1a6620adae37ee37ee4777fb0336e1bb"
        "bc4de65310fc010000006a473044022050d8368cacf9bf1b8fb1f7cfd9aff632"
        "94789eb1760139e7ef41f083726dadc4022067796354aba8f2e02363c5e510aa"
        "7e2830b115472fb31de67d16972867f13945012103e589480b2f746381fca01a"
        "9b12c517b7a482a203c8b2742985da0ac72cc078f2ffffffff02f0c9c4670000"
        "00001976a914d9d78e26df4e4601cf9b26d09c7b280ee764469f88ac80c4600f"
        "000000001976a9141ee32412020a324b93b1a1acfdfff6ab9ca8fac288ac0000"
        "0000010000000364e62ad837f29617bafeae951776e7a6b3019b2da378279215"
        "48d1a5efcf9e5c010000006b48304502204df0dc9b7f61fbb2e4c8b0e09f3426"
        "d625a0191e56c48c338df3214555180eaf022100f21ac1f632201154f3c69e1e"
        "adb59901a34c40f1127e96adc31fac6ae6b11fb4012103893d5a06201d5cf614"
        "00e96fa4a7514fc12ab45166ace618d68b8066c9c585f9ffffffff54b755c392"
        "07d443fd96a8d12c94446a1c6f66e39c95e894c23418d7501f681b010000006b"
        "48304502203267910f55f2297360198fff57a3631be850965344370f732950b4"
        "7795737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d1"
        "5d915da450151d36012103893d5a06201d5cf61400e96fa4a7514fc12ab45166"
        "ace618d68b8066c9c585f9ffffffff0aa14d394a1f0eaf0c4496537f8ab9246d"
        "9663e26acb5f308fccc734b748cc9c010000006c493046022100d64ace8ec2d5"
        "feeb3e868e82b894202db8cb683c414d806b343d02b7ac679de7022100a2dcd3"
        "9940dd28d4e22cce417a0829c1b516c471a3d64d11f2c5d754108bdc0b012103"
        "893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9"
        "ffffffff02c0e1e400000000001976a914884c09d7e1f6420976c40e040c30b2"
        "b62210c3d488ac20300500000000001976a914905f933de850988603aafeeb2f"
        "d7fce61e66fe5d88ac00000000"_base16);

    byte_reader reader(raw);
    auto result = message::block_transactions::from_data(reader, message::block_transactions::version_minimum);
    REQUIRE(result);
    auto expected = std::move(*result);

    auto const data = expected.to_data(message::block_transactions::version_minimum);

    REQUIRE(raw == data);

    byte_reader reader2(data);
    auto result2 = message::block_transactions::from_data(reader2, message::block_transactions::version_minimum - 1);
    REQUIRE( ! result2);
}

TEST_CASE("block transactions from data valid input  success", "[block transactions]") {
    data_chunk raw = to_chunk(
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "020100000001f08e44a96bfb5ae63eda1a6620adae37ee37ee4777fb0336e1bb"
        "bc4de65310fc010000006a473044022050d8368cacf9bf1b8fb1f7cfd9aff632"
        "94789eb1760139e7ef41f083726dadc4022067796354aba8f2e02363c5e510aa"
        "7e2830b115472fb31de67d16972867f13945012103e589480b2f746381fca01a"
        "9b12c517b7a482a203c8b2742985da0ac72cc078f2ffffffff02f0c9c4670000"
        "00001976a914d9d78e26df4e4601cf9b26d09c7b280ee764469f88ac80c4600f"
        "000000001976a9141ee32412020a324b93b1a1acfdfff6ab9ca8fac288ac0000"
        "0000010000000364e62ad837f29617bafeae951776e7a6b3019b2da378279215"
        "48d1a5efcf9e5c010000006b48304502204df0dc9b7f61fbb2e4c8b0e09f3426"
        "d625a0191e56c48c338df3214555180eaf022100f21ac1f632201154f3c69e1e"
        "adb59901a34c40f1127e96adc31fac6ae6b11fb4012103893d5a06201d5cf614"
        "00e96fa4a7514fc12ab45166ace618d68b8066c9c585f9ffffffff54b755c392"
        "07d443fd96a8d12c94446a1c6f66e39c95e894c23418d7501f681b010000006b"
        "48304502203267910f55f2297360198fff57a3631be850965344370f732950b4"
        "7795737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d1"
        "5d915da450151d36012103893d5a06201d5cf61400e96fa4a7514fc12ab45166"
        "ace618d68b8066c9c585f9ffffffff0aa14d394a1f0eaf0c4496537f8ab9246d"
        "9663e26acb5f308fccc734b748cc9c010000006c493046022100d64ace8ec2d5"
        "feeb3e868e82b894202db8cb683c414d806b343d02b7ac679de7022100a2dcd3"
        "9940dd28d4e22cce417a0829c1b516c471a3d64d11f2c5d754108bdc0b012103"
        "893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9"
        "ffffffff02c0e1e400000000001976a914884c09d7e1f6420976c40e040c30b2"
        "b62210c3d488ac20300500000000001976a914905f933de850988603aafeeb2f"
        "d7fce61e66fe5d88ac00000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::block_transactions::from_data(reader, message::block_transactions::version_minimum);
    REQUIRE(result_exp);
    auto expected = std::move(*result_exp);

    auto const data = expected.to_data(message::block_transactions::version_minimum);

    REQUIRE(raw == data);
    byte_reader reader2(data);
    auto result_exp2 = message::block_transactions::from_data(reader2, message::block_transactions::version_minimum);
    REQUIRE(result_exp2);
    auto const result = std::move(*result_exp2);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::block_transactions::version_minimum));
    REQUIRE(expected.serialized_size(message::block_transactions::version_minimum) == result.serialized_size(message::block_transactions::version_minimum));
}

TEST_CASE("block transactions  block hash accessor 1  always  returns initialized value", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions instance(hash, transactions);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("block transactions  block hash accessor 2  always  returns initialized value", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    const message::block_transactions instance(hash, transactions);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("block transactions  block hash setter 1  roundtrip  success", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    message::block_transactions instance;
    REQUIRE(hash != instance.block_hash());
    instance.set_block_hash(hash);
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("block transactions  block hash setter 2  roundtrip  success", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    hash_digest dup_hash = hash;
    message::block_transactions instance;
    REQUIRE(hash != instance.block_hash());
    instance.set_block_hash(std::move(dup_hash));
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("block transactions  transactions accessor 1  always  returns initialized value", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions instance(hash, transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  transactions accessor 2  always  returns initialized value", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    const message::block_transactions instance(hash, transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  transactions setter 1  roundtrip  success", "[block transactions]") {
    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions instance;
    REQUIRE(transactions != instance.transactions());
    instance.set_transactions(transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  transactions setter 2  roundtrip  success", "[block transactions]") {
    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::transaction::list dup_transactions = transactions;
    message::block_transactions instance;
    REQUIRE(transactions != instance.transactions());
    instance.set_transactions(std::move(dup_transactions));
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  operator assign equals  always  matches equivalent", "[block transactions]") {
    hash_digest const hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

    chain::transaction::list const transactions = {
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    message::block_transactions value(hash, transactions);
    REQUIRE(value.is_valid());
    message::block_transactions instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(hash == instance.block_hash());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block transactions  operator boolean equals  duplicates  returns true", "[block transactions]") {
    const message::block_transactions expected(
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    message::block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block transactions  operator boolean equals  differs  returns false", "[block transactions]") {
    const message::block_transactions expected(
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    message::block_transactions instance;
    REQUIRE(instance != expected);
}

TEST_CASE("block transactions  operator boolean not equals  duplicates  returns false", "[block transactions]") {
    const message::block_transactions expected(
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    message::block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block transactions  operator boolean not equals  differs  returns true", "[block transactions]") {
    const message::block_transactions expected(
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    message::block_transactions instance;
    REQUIRE(instance != expected);
}

