// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: base 85 tests

#define BASE85_ENCODED "HelloWorld"
#define BASE85_DECODED {0x86, 0x4F, 0xD2, 0x6F, 0xB5, 0x59, 0xF7, 0x5B}

#define BASE85_ENCODED_INVALID_CHAR "Test\n"
#define BASE85_ENCODED_INVALID_LENGTH "Hello World"
#define BASE85_DECODED_INVALID {0x86, 0x4F, 0xD2, 0x6F, 0xB5, 0x59, 0xF7, 0x5B, 0x42}

TEST_CASE("encode base85 empty test", "[base 85 tests]") {
    auto const result = encode_base85(data_chunk());
    REQUIRE(result);
    REQUIRE(result->empty());
}

TEST_CASE("decode base85 empty test", "[base 85 tests]") {
    auto const result = decode_base85("");
    REQUIRE(result);
    REQUIRE(result->empty());
}

TEST_CASE("encode base85 valid test", "[base 85 tests]") {
    data_chunk decoded(BASE85_DECODED);
    auto const result = encode_base85(decoded);
    REQUIRE(result);
    REQUIRE(*result == BASE85_ENCODED);
}

TEST_CASE("encode base85 invalid test", "[base 85 tests]") {
    data_chunk decoded(BASE85_DECODED_INVALID);
    auto const result = encode_base85(decoded);
    REQUIRE(!result);
    REQUIRE(result.error() == base85_errc::invalid_length);
}

TEST_CASE("decode base85 valid test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED);
    REQUIRE(result);
    REQUIRE(*result == data_chunk(BASE85_DECODED));
}

TEST_CASE("decode base85 invalid char test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED_INVALID_CHAR);
    REQUIRE(!result);
    REQUIRE(result.error() == base85_errc::invalid_character);
}

TEST_CASE("decode base85 invalid length test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED_INVALID_LENGTH);
    REQUIRE(!result);
    REQUIRE(result.error() == base85_errc::invalid_length);
}

// The semicolon is not in the Z85 alphabet, and such characters are treated as
// valid but with zero value in the reference implementation.
TEST_CASE("decode base85 outside alphabet test", "[base 85 tests]") {
    auto const result = decode_base85(";;;;;");
    REQUIRE(result);
    REQUIRE(*result == data_chunk({0, 0, 0, 0}));
}

// End Test Suite

namespace {

constexpr bool base85_constant_evaluation_control() {
    constexpr byte_array<8> bytes{0x86, 0x4f, 0xd2, 0x6f, 0xb5, 0x59, 0xf7, 0x5b};
    if (encode_base85(bytes) != "HelloWorld")
        return false;
    auto const decoded = decode_base85("HelloWorld");
    if (!decoded || *decoded != data_chunk(bytes.begin(), bytes.end()))
        return false;
    if (decode_base85<8>("HelloWorld") != bytes)
        return false;
    if (!decode_base85("")->empty() || encode_base85(byte_span{}) != "")
        return false;
    if (decode_base85("x").error() != base85_errc::invalid_length)
        return false;
    if (encode_base85(std::span(bytes).first(1)).error() != base85_errc::invalid_length)
        return false;
    if (decode_base85("Test\n").error() != base85_errc::invalid_character)
        return false;
    byte_array<10> output{};
    output.front() = output.back() = 0xa5;
    auto const count = decode_base85("HelloWorld", std::span(output).subspan(1, 8));
    if (count != 8 || !std::equal(bytes.begin(), bytes.end(), output.begin() + 1))
        return false;
    if (output.front() != 0xa5 || output.back() != 0xa5)
        return false;
    return decode_base85("HelloWorld", std::span(output).first(7)).error() == base85_errc::invalid_length;
}

static_assert(base85_constant_evaluation_control());

} // namespace

TEST_CASE("base85 compile time and runtime APIs agree", "[base 85 tests]") {
    REQUIRE(base85_constant_evaluation_control());
    for (size_t n = 0; n <= 128; n += 4) {
        data_chunk bytes(n);
        for (size_t i = 0; i < n; ++i)
            bytes[i] = uint8_t(i * 37 + n);
        auto const encoded = encode_base85(bytes);
        REQUIRE(encoded);
        auto const decoded = decode_base85(*encoded);
        REQUIRE(decoded);
        REQUIRE(*decoded == bytes);
        data_chunk output(n + 2, 0xa5);
        auto const count = decode_base85(*encoded, std::span(output).subspan(1, n));
        REQUIRE(count == n);
        REQUIRE(std::equal(bytes.begin(), bytes.end(), output.begin() + 1));
        REQUIRE(output.front() == 0xa5);
        REQUIRE(output.back() == 0xa5);
    }
}

static_assert(""_base85 == byte_array<0>{});
static_assert("00000"_base85 == byte_array<4>{0, 0, 0, 0});
static_assert("HelloWorld"_base85 == byte_array<8>{0x86, 0x4f, 0xd2, 0x6f, 0xb5, 0x59, 0xf7, 0x5b});
static_assert(noexcept("HelloWorld"_base85));
static_assert(noexcept(decode_base85<8>(std::string_view{})));
static_assert(noexcept(decode_base85(std::string_view{}, std::span<uint8_t>{})));
static_assert(!noexcept(encode_base85(byte_span{})));
static_assert(!noexcept(decode_base85(std::string_view{})));

TEST_CASE("base85 checked literal agrees with runtime decoding", "[base 85 tests]") {
    CHECK(encode_base85("HelloWorld"_base85) == "HelloWorld");
    CHECK(decode_base85<8>("HelloWorld") == "HelloWorld"_base85);
}
