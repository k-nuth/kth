// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is named .cpp solely so it can use Catch2 (which is C++).
// Everything inside the test bodies is plain C: no namespaces, no
// templates, no <chrono>, no std::*, no auto, no references, no constexpr.
// Only Catch2's TEST_CASE / REQUIRE macros are C++. The point is that
// these tests must exercise the C-API exactly the way a C consumer would.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <kth/capi/binary.h>
#include <kth/capi/primitives.h>

#include "test_helpers.hpp"

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - default construct is empty", "[C-API Binary]") {
    kth_binary_mut_t b = kth_core_binary_construct_default();
    REQUIRE(kth_core_binary_size(b) == 0u);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - construct from bit string preserves length",
          "[C-API Binary]") {
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("10110100");
    REQUIRE(kth_core_binary_size(b) == 8u);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - construct from size+number preserves bit count",
          "[C-API Binary]") {
    // 12 bits carrying value 0xABC
    kth_binary_mut_t b = kth_core_binary_construct_from_size_number(12u, 0xABCu);
    REQUIRE(kth_core_binary_size(b) == 12u);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - construct from size+blocks roundtrip",
          "[C-API Binary]") {
    uint8_t const src[2] = { 0xA5, 0xC3 };
    kth_binary_mut_t b = kth_core_binary_construct_from_size_blocks(
        16u, src, sizeof(src));
    REQUIRE(kth_core_binary_size(b) == 16u);

    kth_size_t out_size = 0;
    uint8_t* blocks = kth_core_binary_blocks(b, &out_size);
    REQUIRE(out_size == 2u);
    REQUIRE(blocks[0] == 0xA5);
    REQUIRE(blocks[1] == 0xC3);
    kth_core_destruct_array(blocks);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - destruct null is safe", "[C-API Binary]") {
    kth_core_binary_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - encoded returns the bit string",
          "[C-API Binary]") {
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("10110100");
    char* encoded = kth_core_binary_encoded(b);
    REQUIRE(encoded != NULL);
    REQUIRE(strcmp(encoded, "10110100") == 0);
    kth_core_destruct_string(encoded);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - size reports bits, not bytes",
          "[C-API Binary]") {
    // 7-bit binary from a single byte
    kth_binary_mut_t b = kth_core_binary_construct_from_size_number(7u, 0x55u);
    REQUIRE(kth_core_binary_size(b) == 7u);
    kth_core_binary_destruct(b);
}

// ---------------------------------------------------------------------------
// operator[] (renamed to `at`) — bit accessor
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - at() returns individual bits",
          "[C-API Binary]") {
    // 10110100
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("10110100");
    REQUIRE(kth_core_binary_at(b, 0) != 0);  // '1'
    REQUIRE(kth_core_binary_at(b, 1) == 0);  // '0'
    REQUIRE(kth_core_binary_at(b, 2) != 0);  // '1'
    REQUIRE(kth_core_binary_at(b, 3) != 0);  // '1'
    REQUIRE(kth_core_binary_at(b, 4) == 0);  // '0'
    kth_core_binary_destruct(b);
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - is_prefix_of_binary true case",
          "[C-API Binary]") {
    kth_binary_mut_t whole  = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t prefix = kth_core_binary_construct_from_bit_string("101");
    REQUIRE(kth_core_binary_is_prefix_of_binary(prefix, whole) != 0);
    kth_core_binary_destruct(whole);
    kth_core_binary_destruct(prefix);
}

TEST_CASE("C-API Binary - is_prefix_of_binary false case",
          "[C-API Binary]") {
    kth_binary_mut_t whole  = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t prefix = kth_core_binary_construct_from_bit_string("110");
    REQUIRE(kth_core_binary_is_prefix_of_binary(prefix, whole) == 0);
    kth_core_binary_destruct(whole);
    kth_core_binary_destruct(prefix);
}

TEST_CASE("C-API Binary - is_prefix_of_span matches raw byte span",
          "[C-API Binary]") {
    // "10100101 11000011" is the byte span 0xA5 0xC3.
    uint8_t const span[2] = { 0xA5, 0xC3 };
    kth_binary_mut_t prefix = kth_core_binary_construct_from_bit_string("1010");
    REQUIRE(kth_core_binary_is_prefix_of_span(prefix, span, sizeof(span)) != 0);
    kth_core_binary_destruct(prefix);
}

TEST_CASE("C-API Binary - is_prefix_of_uint32 matches leading bits",
          "[C-API Binary]") {
    // binary::is_prefix_of(uint32_t) converts `value` to little-endian
    // bytes, so the prefix compares against the low byte first. 0xA5 is
    // 10100101 — the first 4 bits are "1010".
    uint32_t const value = 0x000000A5u;
    kth_binary_mut_t prefix = kth_core_binary_construct_from_bit_string("1010");
    REQUIRE(kth_core_binary_is_prefix_of_uint32(prefix, value) != 0);
    kth_core_binary_destruct(prefix);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - copy preserves contents",
          "[C-API Binary]") {
    kth_binary_mut_t original = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t copy     = kth_core_binary_copy(original);
    REQUIRE(kth_core_binary_equals(original, copy) != 0);
    REQUIRE(kth_core_binary_size(copy) == kth_core_binary_size(original));
    kth_core_binary_destruct(copy);
    kth_core_binary_destruct(original);
}

TEST_CASE("C-API Binary - equals identical is true, different is false",
          "[C-API Binary]") {
    kth_binary_mut_t a = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t c = kth_core_binary_construct_from_bit_string("11110000");
    REQUIRE(kth_core_binary_equals(a, b) != 0);
    REQUIRE(kth_core_binary_equals(a, c) == 0);
    kth_core_binary_destruct(a);
    kth_core_binary_destruct(b);
    kth_core_binary_destruct(c);
}

TEST_CASE("C-API Binary - less gives a total order",
          "[C-API Binary]") {
    kth_binary_mut_t a = kth_core_binary_construct_from_bit_string("1010");
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("1011");
    REQUIRE(kth_core_binary_less(a, b) != 0);
    REQUIRE(kth_core_binary_less(b, a) == 0);
    kth_core_binary_destruct(a);
    kth_core_binary_destruct(b);
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - resize changes the bit count",
          "[C-API Binary]") {
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("10110100");
    kth_core_binary_resize(b, 4u);
    REQUIRE(kth_core_binary_size(b) == 4u);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - append grows the size",
          "[C-API Binary]") {
    kth_binary_mut_t a = kth_core_binary_construct_from_bit_string("1010");
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("0101");
    kth_core_binary_append(a, b);
    REQUIRE(kth_core_binary_size(a) == 8u);
    kth_core_binary_destruct(a);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - prepend grows the size",
          "[C-API Binary]") {
    kth_binary_mut_t a = kth_core_binary_construct_from_bit_string("1010");
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("0101");
    kth_core_binary_prepend(a, b);
    REQUIRE(kth_core_binary_size(a) == 8u);
    kth_core_binary_destruct(a);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - substring returns a new binary",
          "[C-API Binary]") {
    kth_binary_mut_t whole = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t sub   = kth_core_binary_substring(whole, 2u, 4u);
    REQUIRE(kth_core_binary_size(sub) == 4u);
    kth_core_binary_destruct(sub);
    kth_core_binary_destruct(whole);
}

TEST_CASE("C-API Binary - shift_left drops leading bits",
          "[C-API Binary]") {
    // kth::binary::shift_left(n) makes the binary `n` bits shorter
    // by dropping the `n` leading bits. "10110100" shifted left by 2
    // becomes "110100" (6 bits).
    kth_binary_mut_t b        = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t expected = kth_core_binary_construct_from_bit_string("110100");
    kth_core_binary_shift_left(b, 2u);
    REQUIRE(kth_core_binary_equals(b, expected) != 0);
    kth_core_binary_destruct(expected);
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - shift_right prepends zeros",
          "[C-API Binary]") {
    // kth::binary::shift_right(n) is NOT the inverse of shift_left:
    // it grows the binary by `n` bits, prepending zeros. "10110100"
    // shifted right by 2 becomes "0010110100" (10 bits).
    kth_binary_mut_t b        = kth_core_binary_construct_from_bit_string("10110100");
    kth_binary_mut_t expected = kth_core_binary_construct_from_bit_string("0010110100");
    kth_core_binary_shift_right(b, 2u);
    REQUIRE(kth_core_binary_equals(b, expected) != 0);
    kth_core_binary_destruct(expected);
    kth_core_binary_destruct(b);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - blocks_size rounds up to full bytes",
          "[C-API Binary]") {
    REQUIRE(kth_core_binary_blocks_size(0u)  == 0u);
    REQUIRE(kth_core_binary_blocks_size(1u)  == 1u);
    REQUIRE(kth_core_binary_blocks_size(8u)  == 1u);
    REQUIRE(kth_core_binary_blocks_size(9u)  == 2u);
    REQUIRE(kth_core_binary_blocks_size(16u) == 2u);
    REQUIRE(kth_core_binary_blocks_size(17u) == 3u);
}

TEST_CASE("C-API Binary - is_base2 accepts 0/1 strings",
          "[C-API Binary]") {
    REQUIRE(kth_core_binary_is_base2("10110100") != 0);
    REQUIRE(kth_core_binary_is_base2("") != 0);
    REQUIRE(kth_core_binary_is_base2("102") == 0);
    REQUIRE(kth_core_binary_is_base2("abc") == 0);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Binary - at() out of bounds aborts",
          "[C-API Binary][precondition]") {
    kth_binary_mut_t b = kth_core_binary_construct_from_bit_string("1010");
    KTH_EXPECT_ABORT(kth_core_binary_at(b, 4));
    kth_core_binary_destruct(b);
}

TEST_CASE("C-API Binary - at() on empty binary aborts",
          "[C-API Binary][precondition]") {
    kth_binary_mut_t b = kth_core_binary_construct_default();
    KTH_EXPECT_ABORT(kth_core_binary_at(b, 0));
    kth_core_binary_destruct(b);
}
