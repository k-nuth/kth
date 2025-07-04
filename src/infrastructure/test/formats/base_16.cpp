// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: base 16 tests

TEST_CASE("infrastructure base16 literal compilation", "[infrastructure][base16]") {
    auto result = base16_literal("01ff42bc");
    const byte_array<4> expected
    {
        {
            0x01, 0xff, 0x42, 0xbc
        }
    };
    REQUIRE(result == expected);
}

TEST_CASE("infrastructure base16 decode odd length string should fail", "[infrastructure][base16]") {
    auto const& hex_str = "10a7fd15cb45bda9e90e19a15";
    data_chunk data;
    REQUIRE(!decode_base16(data, hex_str));
}

TEST_CASE("infrastructure base16 encode and decode short hash", "[infrastructure][base16]") {
    auto const& hex_str = "f85beb6356d0813ddb0dbb14230a249fe931a135";
    short_hash hash;
    REQUIRE(decode_base16(hash, hex_str));
    REQUIRE(encode_base16(hash) == hex_str);
    short_hash const expected
    {
        {
           0xf8, 0x5b, 0xeb, 0x63, 0x56, 0xd0, 0x81, 0x3d, 0xdb, 0x0d,
           0xbb, 0x14, 0x23, 0x0a, 0x24, 0x9f, 0xe9, 0x31, 0xa1, 0x35
        }
    };
    REQUIRE(hash == expected);
}

// TODO: this should be tested for correctness, not just round-tripping.
TEST_CASE("infrastructure base16 encode and decode round trip", "[infrastructure][base16]") {
    auto const& hex_str = "10a7fd15cb45bda9e90e19a15f";
    data_chunk data;
    REQUIRE(decode_base16(data, hex_str));
    REQUIRE(encode_base16(data) == hex_str);
}

TEST_CASE("infrastructure base16 decode to fixed size array", "[infrastructure][base16]") {
    byte_array<4> converted;
    REQUIRE(decode_base16(converted, "01ff42bc"));
    const byte_array<4> expected
    {
        {
            0x01, 0xff, 0x42, 0xbc
        }
    };
    REQUIRE(converted == expected);
}

// End Test Suite
