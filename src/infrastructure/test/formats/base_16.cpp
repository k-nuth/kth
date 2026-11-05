// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Compile-time validation tests for base16_literal concept
static_assert(base16_literal<"deadbeef">);  // valid hex
static_assert(base16_literal<"DEADBEEF">);  // valid uppercase
static_assert(base16_literal<"DeAdBeEf">);  // valid mixed case
static_assert(base16_literal<"">);          // valid empty
static_assert(base16_literal<"00">);        // valid single byte
static_assert(!base16_literal<"abc">);      // invalid: odd length
static_assert(!base16_literal<"abcde">);    // invalid: odd length
static_assert(!base16_literal<"xyz">);      // invalid: bad characters
static_assert(!base16_literal<"deadbeXf">); // invalid: bad character in middle
static_assert(!base16_literal<"ghijklmn">); // invalid: all bad characters

// Compile-time validation tests for hash_literal concept
static_assert(hash_literal<"0000000000000000000000000000000000000000000000000000000000000000">); // valid: 64 zeros
static_assert(hash_literal<"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f">); // valid: genesis hash
static_assert(!hash_literal<"deadbeef">); // invalid: too short (8 chars)
static_assert(!hash_literal<"">);         // invalid: empty
static_assert(!hash_literal<"00">);       // invalid: too short (2 chars)
static_assert(
    !hash_literal<"000000000000000000000000000000000000000000000000000000000000000">); // invalid: 63 chars (odd)
static_assert(!hash_literal<"00000000000000000000000000000000000000000000000000000000000000000">); // invalid: 65 chars
static_assert(!hash_literal<"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26X">);  // invalid: bad char

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
    REQUIRE(!result);
    REQUIRE(result.error() == base16_errc::odd_length);
}

TEST_CASE("base16 decode invalid character should fail", "[infrastructure][base16]") {
    auto const result = decode_base16("deadbeXf");
    REQUIRE(!result);
    REQUIRE(result.error() == base16_errc::invalid_character);
}

TEST_CASE("base16 encode and decode short hash", "[infrastructure][base16]") {
    constexpr auto expected = std::to_array<uint8_t>({0xf8, 0x5b, 0xeb, 0x63, 0x56, 0xd0, 0x81, 0x3d, 0xdb, 0x0d, 0xbb,
        0x14, 0x23, 0x0a, 0x24, 0x9f, 0xe9, 0x31, 0xa1, 0x35});
    constexpr auto hex_str = "f85beb6356d0813ddb0dbb14230a249fe931a135";

    static_assert("f85beb6356d0813ddb0dbb14230a249fe931a135"_base16 == expected);

    auto const result = decode_base16(hex_str);
    REQUIRE(result);
    REQUIRE(*result == to_chunk(expected));
    REQUIRE(encode_base16(*result) == hex_str);
}

TEST_CASE("base16 encode and decode round trip", "[infrastructure][base16]") {
    constexpr auto expected =
        std::to_array<uint8_t>({0x10, 0xa7, 0xfd, 0x15, 0xcb, 0x45, 0xbd, 0xa9, 0xe9, 0x0e, 0x19, 0xa1, 0x5f});
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
    auto const result = decode_base16<4>("01ff42"); // 3 bytes, expected 4
    REQUIRE(!result);
    REQUIRE(result.error() == base16_errc::size_mismatch);
    CHECK(decode_base16<4>("0ff").error() == base16_errc::odd_length);
    CHECK(decode_hash(std::string(62, '0')).error() == base16_errc::size_mismatch);
}

// End Test Suite

static_assert(decode_base16<2>("00ff").value() == std::array<uint8_t, 2>{0, 255});
static_assert(encode_base16("00ff"_base16) == "00ff");
static_assert(decode_base16("0").error() == base16_errc::odd_length);
static_assert(decode_base16("xz").error() == base16_errc::invalid_character);
static_assert(encode_hash("0000000000000000000000000000000000000000000000000000000000000001"_hash) ==
              "0000000000000000000000000000000000000000000000000000000000000001");

TEST_CASE("base16 runtime and constexpr APIs share common errors", "[infrastructure][base16]") {
    std::array<uint8_t, 2> bytes{};
    CHECK(decode_base16("00ff", bytes) == 2);
    CHECK(decode_base16("00ff", std::span(bytes).first(1)).error() == base16_errc::size_mismatch);
    CHECK(decode_base16("00xz").error() == base16_errc::invalid_character);
    CHECK(decode_base16("0").error() == base16_errc::odd_length);
    CHECK(decode_base16<2>("00ff") == bytes);
}

static_assert(noexcept("ff"_base16));
static_assert(noexcept(decode_base16<1>(std::string_view{})));
static_assert(noexcept(decode_base16(std::string_view{}, std::span<uint8_t>{})));
static_assert(!noexcept(encode_base16(byte_span{})));
static_assert(!noexcept(decode_base16(std::string_view{})));
static_assert(is_base16(std::string_view("deadBEEF")));
static_assert(!is_base16(std::string_view("deadBEEX")));

namespace {

constexpr bool base16_span_capacity_control() {
    byte_array<5> output{0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    auto const count = decode_base16("00ff", std::span(output).subspan(1, 3));
    if (count != 2 || output != byte_array<5>{0xa5, 0, 0xff, 0xa5, 0xa5})
        return false;
    return decode_base16("", output) == 0;
}

static_assert(base16_span_capacity_control());

} // namespace

TEST_CASE("base16 span accepts spare capacity and preserves untouched bytes", "[base16]") {
    CHECK(base16_span_capacity_control());
    CHECK(decode_base16<3>("00ff").error() == base16_errc::size_mismatch);
}

TEST_CASE("encode_hash accepts numeric uint256 values", "[base16]") {
    REQUIRE(encode_hash(uint256_t(0)) == std::string(64, '0'));
    REQUIRE(encode_hash(uint256_t(1)) == std::string(63, '0') + "1");
    REQUIRE(encode_hash(~uint256_t(0)) == std::string(64, 'f'));
    constexpr std::string_view digits = "0123456789abcdef";
    for (size_t bit = 0; bit < 256; ++bit) {
        std::string expected(64, '0');
        expected[63 - bit / 4] = digits[1u << (bit % 4)];
        REQUIRE(encode_hash(uint256_t(1) << bit) == expected);
    }
}

static_assert(encode_base16(uint32_t(0)) == "00000000");
static_assert(encode_base16(uint32_t(1)) == "00000001");
static_assert(encode_base16(uint32_t(0x1d00ffff)) == "1d00ffff");
static_assert(encode_base16(uint32_t(0xffffffff)) == "ffffffff");

TEST_CASE("encode_base16 encodes uint32 values with eight digits", "[base16]") {
    REQUIRE(encode_base16(uint32_t(0)) == "00000000");
    REQUIRE(encode_base16(uint32_t(1)) == "00000001");
    REQUIRE(encode_base16(uint32_t(0x12345678)) == "12345678");
    REQUIRE(encode_base16(uint32_t(0x1d00ffff)) == "1d00ffff");
    REQUIRE(encode_base16(uint32_t(0xffffffff)) == "ffffffff");
    constexpr std::string_view digits = "0123456789abcdef";
    for (size_t bit = 0; bit < 32; ++bit) {
        std::string expected(8, '0');
        expected[7 - bit / 4] = digits[1u << (bit % 4)];
        REQUIRE(encode_base16(uint32_t(1) << bit) == expected);
    }
}
