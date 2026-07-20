// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: base 85 tests

#define BASE85_ENCODED "HelloWorld"
#define BASE85_DECODED \
{ \
    0x86, 0x4F, 0xD2, 0x6F, 0xB5, 0x59, 0xF7, 0x5B \
}

#define BASE85_ENCODED_INVALID_CHAR "Test\n"
#define BASE85_ENCODED_INVALID_LENGTH "Hello World"
#define BASE85_DECODED_INVALID \
{ \
    0x86, 0x4F, 0xD2, 0x6F, 0xB5, 0x59, 0xF7, 0x5B, 0x42 \
}

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
    REQUIRE( ! result);
    REQUIRE(result.error() == base85_errc::invalid_length);
}

TEST_CASE("decode base85 valid test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED);
    REQUIRE(result);
    REQUIRE(*result == data_chunk(BASE85_DECODED));
}

TEST_CASE("decode base85 invalid char test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED_INVALID_CHAR);
    REQUIRE( ! result);
    REQUIRE(result.error() == base85_errc::invalid_character);
}

TEST_CASE("decode base85 invalid length test", "[base 85 tests]") {
    auto const result = decode_base85(BASE85_ENCODED_INVALID_LENGTH);
    REQUIRE( ! result);
    REQUIRE(result.error() == base85_errc::invalid_length);
}

// The semicolon is not in the Z85 alphabet, and such characters are treated as
// valid but with zero value in the reference implementation.
TEST_CASE("decode base85 outside alphabet test", "[base 85 tests]") {
    auto const result = decode_base85(";;;;;");
    REQUIRE(result);
    REQUIRE(*result == data_chunk({ 0, 0, 0, 0 }));
}

// End Test Suite
