// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: headers tests

TEST_CASE("headers  constructor 1  always  initialized invalid", "[headers]") {
    headers instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("headers  constructor 2  always  equals params", "[headers]") {
    header::list const expected{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    headers instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.elements() == expected);
}

TEST_CASE("headers  constructor 3  always  equals params", "[headers]") {
    header::list const expected{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    headers instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(instance.elements().size() == 2u);
}

TEST_CASE("headers  constructor 4  always  equals params", "[headers]") {
    headers instance(
        {header(
             10u,
             hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
             hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
             531234u,
             6523454u,
             68644u),
         header(
             11234u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
             753234u,
             4356344u,
             34564u)});

    REQUIRE(instance.is_valid());
    REQUIRE(instance.elements().size() == 2u);
}

TEST_CASE("headers  constructor 5  always  equals params", "[headers]") {
    headers const expected(
        {header(
             10u,
             hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
             hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
             531234u,
             6523454u,
             68644u),
         header(
             11234u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
             753234u,
             4356344u,
             34564u)});

    headers instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("headers  constructor 6  always  equals params", "[headers]") {
    headers expected(
        {header(
             10u,
             hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
             hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
             531234u,
             6523454u,
             68644u),
         header(
             11234u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
             753234u,
             4356344u,
             34564u)});

    headers instance(std::move(expected));
    REQUIRE(instance.elements().size() == 2u);
}

TEST_CASE("headers from data insufficient bytes  failure", "[headers]") {
    data_chunk const raw{0xab, 0xcd};
    headers instance{};
    byte_reader reader(raw);
    auto result = headers::from_data(reader, headers::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("headers from data insufficient version  failure", "[headers]") {
    static headers const expected{
        {10,
         hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
         hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
         531234,
         6523454,
         68644}};

    data_chunk const data = expected.to_data(headers::version_minimum);
    headers instance{};
    byte_reader reader(data);
    auto result = headers::from_data(reader, headers::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("headers from data valid input  success", "[headers]") {
    static headers const expected{
        {10,
         hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
         hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
         531234,
         6523454,
         68644}};

    static auto const version = headers::version_minimum;
    auto const data = expected.to_data(version);
    byte_reader reader(data);
    auto const result_exp = headers::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(result == expected);
    REQUIRE(result.serialized_size(version) == data.size());
    REQUIRE(result.serialized_size(version) == expected.serialized_size(version));
}



TEST_CASE("headers  elements accessor 1  always  returns initialized value", "[headers]") {
    header::list const expected{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    message::headers instance(expected);
    REQUIRE(instance.elements() == expected);
}

TEST_CASE("headers  elements accessor 2  always  returns initialized value", "[headers]") {
    header::list const expected{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    const message::headers instance(expected);
    REQUIRE(instance.elements() == expected);
}

TEST_CASE("headers  command setter 1  roundtrip  success", "[headers]") {
    header::list const expected{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    message::headers instance;
    REQUIRE(instance.elements() != expected);
    instance.set_elements(expected);
    REQUIRE(instance.elements() == expected);
}

TEST_CASE("headers  command setter 2  roundtrip  success", "[headers]") {
    header::list values{
        header(
            10u,
            hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
            hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
            531234u,
            6523454u,
            68644u),
        header(
            11234u,
            hash_literal("abababababababababababababababababababababababababababababababab"),
            hash_literal("fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"),
            753234u,
            4356344u,
            34564u)};

    message::headers instance;
    REQUIRE(instance.elements().empty());
    instance.set_elements(std::move(values));
    REQUIRE(instance.elements().size() == 2u);
}

TEST_CASE("headers  operator assign equals  always  matches equivalent", "[headers]") {
    message::headers value(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    REQUIRE(value.is_valid());
    message::headers instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("headers  operator boolean equals  duplicates  returns true", "[headers]") {
    const message::headers expected(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    message::headers instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("headers  operator boolean equals  differs  returns false", "[headers]") {
    const message::headers expected(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    message::headers instance;
    REQUIRE(instance != expected);
}

TEST_CASE("headers  operator boolean not equals  duplicates  returns false", "[headers]") {
    const message::headers expected(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    message::headers instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("headers  operator boolean not equals  differs  returns true", "[headers]") {
    const message::headers expected(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    message::headers instance;
    REQUIRE(instance != expected);
}

TEST_CASE("headers  to hashes  empty  returns empty list", "[headers]") {
    message::headers instance;
    hash_list result;
    instance.to_hashes(result);
    REQUIRE(result.empty());
}

TEST_CASE("headers  to hashes  non empty  returns header hash list", "[headers]") {
    hash_list const expected{
        hash_literal("108127a4f5955a546b78807166d8cb9cd3eee1ed530c14d51095bc798685f4d6"),
        hash_literal("37ec64a548b6419769b152d70efc4c356f74c7fda567711d98cac3c55c34a890"),
        hash_literal("d9bbb4b47ca45ec8477cba125262b07b17daae944b54d1780e0a6373d2eed879")};

    const message::headers instance(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    hash_list result;
    instance.to_hashes(result);
    REQUIRE(result.size() == expected.size());
    REQUIRE(result == expected);
}

TEST_CASE("headers  to inventory  empty  returns empty list", "[headers]") {
    message::headers instance;
    inventory_vector::list result;
    instance.to_inventory(result, inventory_vector::type_id::block);
    REQUIRE(0 == result.size());
}

TEST_CASE("headers  to inventory  non empty  returns header hash inventory list", "[headers]") {
    inventory_vector::list const expected{
        inventory_vector(inventory_vector::type_id::block, hash_literal("108127a4f5955a546b78807166d8cb9cd3eee1ed530c14d51095bc798685f4d6")),
        inventory_vector(inventory_vector::type_id::block, hash_literal("37ec64a548b6419769b152d70efc4c356f74c7fda567711d98cac3c55c34a890")),
        inventory_vector(inventory_vector::type_id::block, hash_literal("d9bbb4b47ca45ec8477cba125262b07b17daae944b54d1780e0a6373d2eed879"))};

    headers const instance(
        {header{
             1u,
             hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
             hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
             10u,
             100u,
             1000u},
         header{
             2u,
             hash_literal("abababababababababababababababababababababababababababababababab"),
             hash_literal("babababababababababababababababababababababababababababababababa"),
             20u,
             200u,
             2000u},
         header{
             3u,
             hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
             hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
             30u,
             300u,
             3000u}});

    inventory_vector::list result;
    instance.to_inventory(result, inventory_vector::type_id::block);
    REQUIRE(result.size() == expected.size());
    REQUIRE(result == expected);
}

TEST_CASE("headers  is sequential  empty  true", "[headers]") {
    static headers const instance;
    REQUIRE(instance.elements().empty());
    REQUIRE(instance.is_sequential());
}

TEST_CASE("headers  is sequential  single  true", "[headers]") {
    static header const first{
        1u,
        hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
        hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
        10u,
        100u,
        1000u};

    headers const instance({first});
    REQUIRE(instance.is_sequential());
}

TEST_CASE("headers  is sequential  sequential  true", "[headers]") {
    static header const first{
        1u,
        hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
        hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
        10u,
        100u,
        1000u};

    static header const second{
        2u,
        first.hash(),
        hash_literal("babababababababababababababababababababababababababababababababa"),
        20u,
        200u,
        2000u};

    static header const third{
        3u,
        second.hash(),
        hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
        30u,
        300u,
        3000u};

    headers const instance({first, second, third});
    REQUIRE(instance.is_sequential());
}

TEST_CASE("headers  is sequential  disordered  false", "[headers]") {
    static header const first{
        1u,
        hash_literal("f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"),
        hash_literal("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
        10u,
        100u,
        1000u};

    static header const second{
        2u,
        hash_literal("abababababababababababababababababababababababababababababababab"),
        hash_literal("babababababababababababababababababababababababababababababababa"),
        20u,
        200u,
        2000u};

    static header const third{
        3u,
        hash_literal("e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2"),
        hash_literal("7373737373737373737373737373737373737373737373737373737373737373"),
        30u,
        300u,
        3000u};

    headers const instance({first, second, third});
    REQUIRE( ! instance.is_sequential());
}

// End Test Suite
