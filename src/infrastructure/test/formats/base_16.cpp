// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Compile-time validation tests for base16_literal concept
static_assert(base16_literal<"deadbeef">);                // valid hex
static_assert(base16_literal<"DEADBEEF">);                // valid uppercase
static_assert(base16_literal<"DeAdBeEf">);                // valid mixed case
static_assert(base16_literal<"">);                        // valid empty
static_assert(base16_literal<"00">);                      // valid single byte
static_assert(!base16_literal<"abc">);                    // invalid: odd length
static_assert(!base16_literal<"abcde">);                  // invalid: odd length
static_assert(!base16_literal<"xyz">);                    // invalid: bad characters
static_assert(!base16_literal<"deadbeXf">);               // invalid: bad character in middle
static_assert(!base16_literal<"ghijklmn">);               // invalid: all bad characters

// Compile-time validation tests for hash_literal concept
static_assert(hash_literal<"0000000000000000000000000000000000000000000000000000000000000000">);   // valid: 64 zeros
static_assert(hash_literal<"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f">);   // valid: genesis hash
static_assert(!hash_literal<"deadbeef">);                 // invalid: too short (8 chars)
static_assert(!hash_literal<"">);                         // invalid: empty
static_assert(!hash_literal<"00">);                       // invalid: too short (2 chars)
static_assert(!hash_literal<"000000000000000000000000000000000000000000000000000000000000000">);   // invalid: 63 chars (odd)
static_assert(!hash_literal<"00000000000000000000000000000000000000000000000000000000000000000">); // invalid: 65 chars
static_assert(!hash_literal<"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26X">); // invalid: bad char

// Note: UDL operators (_base16, _hash) are tested for compile-time failures
// in test/compile_fail/test_compile_failures.py which verifies that invalid
// expressions fail to compile as expected.

// Start Test Suite: base 16 tests

TEST_CASE("base16 decode basic", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0x01, 0xff, 0x42, 0xbc});

    static_assert("01ff42bc"_base16 == expected);

    auto const result = decode_base16("01ff42bc");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode deadbeef", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0xde, 0xad, 0xbe, 0xef});

    static_assert("deadbeef"_base16 == expected);

    auto const result = decode_base16("deadbeef");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode single zero byte", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0x00});

    static_assert("00"_base16 == expected);

    auto const result = decode_base16("00");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode single ff byte", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0xff});

    static_assert("ff"_base16 == expected);

    auto const result = decode_base16("ff");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode empty", "[infrastructure][base16]") {
    constexpr auto expected = std::array<uint8_t, 0>{};

    static_assert(""_base16 == expected);

    auto const result = decode_base16("");
    REQUIRE(result);
    REQUIRE(result->empty());
}

TEST_CASE("base16 decode uppercase", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0xde, 0xad, 0xbe, 0xef});

    static_assert("DEADBEEF"_base16 == expected);

    auto const result = decode_base16("DEADBEEF");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode mixed case", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0xde, 0xad, 0xbe, 0xef});

    static_assert("DeAdBeEf"_base16 == expected);

    auto const result = decode_base16("DeAdBeEf");
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
}

TEST_CASE("base16 decode odd length string should fail", "[infrastructure][base16]") {
    auto const result = decode_base16("10a7fd15cb45bda9e90e19a15");
    REQUIRE( ! result);
    REQUIRE(result.error() == base16_errc::odd_length);
}

TEST_CASE("base16 decode invalid character should fail", "[infrastructure][base16]") {
    auto const result = decode_base16("deadbeXf");
    REQUIRE( ! result);
    REQUIRE(result.error() == base16_errc::invalid_character);
}

TEST_CASE("base16 encode and decode short hash", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({
        0xf8, 0x5b, 0xeb, 0x63, 0x56, 0xd0, 0x81, 0x3d, 0xdb, 0x0d,
        0xbb, 0x14, 0x23, 0x0a, 0x24, 0x9f, 0xe9, 0x31, 0xa1, 0x35});
    constexpr auto hex_str = "f85beb6356d0813ddb0dbb14230a249fe931a135";

    static_assert("f85beb6356d0813ddb0dbb14230a249fe931a135"_base16 == expected);

    auto const result = decode_base16(hex_str);
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
    REQUIRE(encode_base16(*result) == hex_str);
}

TEST_CASE("base16 encode and decode round trip", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({
        0x10, 0xa7, 0xfd, 0x15, 0xcb, 0x45, 0xbd, 0xa9,
        0xe9, 0x0e, 0x19, 0xa1, 0x5f
});
    constexpr auto hex_str = "10a7fd15cb45bda9e90e19a15f";

    static_assert("10a7fd15cb45bda9e90e19a15f"_base16 == expected);

    auto const result = decode_base16(hex_str);
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
    REQUIRE(encode_base16(*result) == hex_str);
}

TEST_CASE("base16 decode to fixed size array", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0x01, 0xff, 0x42, 0xbc});

    static_assert("01ff42bc"_base16 == expected);

    auto const result = decode_base16<4>("01ff42bc");
    REQUIRE(result);
    REQUIRE(*result == expected);
}

TEST_CASE("base16 decode to fixed size array wrong size should fail", "[infrastructure][base16]") {
    auto const result = decode_base16<4>("01ff42");  // 3 bytes, expected 4
    REQUIRE( ! result);
    REQUIRE(result.error() == base16_errc::odd_length);
}

// End Test Suite
