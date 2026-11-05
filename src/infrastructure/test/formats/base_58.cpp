// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: base 58 tests

void encdec_test(std::string const &hex, std::string const &encoded) {
    auto const data = decode_base16(hex);
    REQUIRE(data);
    REQUIRE(encode_base58(*data) == encoded);
    auto const decoded = decode_base58(encoded);
    REQUIRE(decoded);
    REQUIRE(*decoded == *data);
}

TEST_CASE("infrastructure base58 encode and decode various test vectors", "[infrastructure][base58]") {
    encdec_test("", "");
    encdec_test("61", "2g");
    encdec_test("626262", "a3gV");
    encdec_test("636363", "aPEr");
    encdec_test("73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2");
    encdec_test("00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L");
    encdec_test("516b6fcd0f", "ABnLTmg");
    encdec_test("bf4f89001e670274dd", "3SEo3LWLoPntC");
    encdec_test("572e4794", "3EFU7m");
    encdec_test("ecac89cad93923c02321", "EJDM8drfXA6uyA");
    encdec_test("10c8511e", "Rt5zm");
    encdec_test("00000000000000000000", "1111111111");
}

TEST_CASE("infrastructure base58 encode and decode bitcoin address", "[infrastructure][base58]") {
    data_chunk const pubkey{{0x00, 0x5c, 0xc8, 0x7f, 0x4a, 0x3f, 0xdf, 0xe3, 0xa2, 0x34, 0x6b, 0x69, 0x53, 0x26, 0x7c,
        0xa8, 0x67, 0x28, 0x26, 0x30, 0xd3, 0xf9, 0xb7, 0x8e, 0x64}};
    std::string const address = "19TbMSWwHvnxAKy12iNm3KdbGfzfaMFViT";
    REQUIRE(encode_base58(pubkey) == address);
    auto const decoded = decode_base58(address);
    REQUIRE(decoded);
    REQUIRE(*decoded == pubkey);
}

TEST_CASE("infrastructure base58 character validation", "[infrastructure][base58]") {
    SECTION("valid base58 characters should be recognized") {
        std::string const base58_chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        for (char const ch : base58_chars) {
            REQUIRE(is_base58(ch));
        }
    }

    SECTION("invalid base58 characters should be rejected") {
        std::string const non_base58_chars = "0OIl+- //#";
        for (char const ch : non_base58_chars) {
            REQUIRE(!is_base58(ch));
        }
    }

    SECTION("base58 string validation") {
        REQUIRE(is_base58("abcdjkk11"));
        REQUIRE(!is_base58("abcdjkk011"));
    }
}

TEST_CASE("infrastructure base58 decode to fixed size array", "[infrastructure][base58]") {
    auto const converted = decode_base58<25>("19TbMSWwHvnxAKy12iNm3KdbGfzfaMFViT");
    REQUIRE(converted);
    const byte_array<25> expected{{0x00, 0x5c, 0xc8, 0x7f, 0x4a, 0x3f, 0xdf, 0xe3, 0xa2, 0x34, 0x6b, 0x69, 0x53, 0x26,
        0x7c, 0xa8, 0x67, 0x28, 0x26, 0x30, 0xd3, 0xf9, 0xb7, 0x8e, 0x64}};
    REQUIRE(*converted == expected);
}

// End Test Suite

namespace {

constexpr bool base58_constant_evaluation_control() {
    constexpr byte_array<3> bytes{0, 0, 97};
    if (encode_base58(bytes) != "112g" || !is_base58("112g") || is_base58('0'))
        return false;
    auto const decoded = decode_base58("112g");
    if (!decoded || *decoded != data_chunk(bytes.begin(), bytes.end()))
        return false;
    if (decode_base58<3>("112g") != bytes)
        return false;
    if (!decode_base58("")->empty() || encode_base58(byte_span{}) != "")
        return false;
    if (decode_base58("0").error() != base58_errc::invalid_character)
        return false;
    byte_array<4> output{0xa5, 0, 0, 0xa5};
    auto const count = decode_base58("2g", std::span(output).subspan(1, 2));
    if (count != 1 || output[1] != 97 || output.front() != 0xa5 || output.back() != 0xa5)
        return false;
    return decode_base58("2g", std::span(output).first(0)).error() == base58_errc::size_mismatch;
}

static_assert(base58_constant_evaluation_control());
static_assert(base58_literal("11") == byte_array<2>{0, 0});

} // namespace

TEST_CASE("base58 compile time and runtime APIs agree", "[base58]") {
    REQUIRE(base58_constant_evaluation_control());
    for (size_t n = 0; n <= 128; ++n) {
        data_chunk bytes(n);
        for (size_t i = 0; i < n; ++i)
            bytes[i] = uint8_t(i * 37 + n);
        auto const encoded = encode_base58(bytes);
        auto const decoded = decode_base58(encoded);
        REQUIRE(decoded);
        REQUIRE(*decoded == bytes);
        data_chunk output(encoded.size() + 3, 0xa5);
        auto const count = decode_base58(encoded, std::span(output).subspan(1, encoded.size() + 1));
        REQUIRE(count == n);
        REQUIRE(std::equal(bytes.begin(), bytes.end(), output.begin() + 1));
        REQUIRE(output.front() == 0xa5);
        REQUIRE(output.back() == 0xa5);
    }
}

static_assert(""_base58 == byte_array<0>{});
static_assert("111"_base58 == byte_array<3>{0, 0, 0});
static_assert("2g"_base58 == byte_array<1>{97});
static_assert("112g"_base58 == byte_array<3>{0, 0, 97});
static_assert(noexcept("2g"_base58));
static_assert(noexcept(base58_literal("11")));
static_assert(noexcept(decode_base58<1>(std::string_view{})));
static_assert(noexcept(decode_base58(std::string_view{}, std::span<uint8_t>{})));
static_assert(!noexcept(encode_base58(byte_span{})));
static_assert(!noexcept(decode_base58(std::string_view{})));

TEST_CASE("base58 checked literal uses exact decoded size", "[base58]") {
    CHECK(encode_base58("2g"_base58) == "2g");
    CHECK(encode_base58("112g"_base58) == "112g");
    CHECK(decode_base58<1>("2g") == "2g"_base58);
}
