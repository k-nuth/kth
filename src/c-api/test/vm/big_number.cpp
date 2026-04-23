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
#include <stdlib.h>
#include <string.h>

#include <kth/capi/primitives.h>
#include <kth/capi/vm/big_number.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - default construct yields zero",
          "[C-API BigNumber][lifecycle]") {
    kth_big_number_mut_t n = kth_vm_big_number_construct_default();
    REQUIRE(n != NULL);
    REQUIRE(kth_vm_big_number_is_zero(n) != 0);
    REQUIRE(kth_vm_big_number_is_nonzero(n) == 0);
    REQUIRE(kth_vm_big_number_sign(n) == 0);
    kth_vm_big_number_destruct(n);
}

TEST_CASE("C-API BigNumber - destruct(NULL) is a no-op",
          "[C-API BigNumber][lifecycle]") {
    kth_vm_big_number_destruct(NULL);
}

TEST_CASE("C-API BigNumber - construct_from_value carries a signed int64",
          "[C-API BigNumber][lifecycle]") {
    kth_big_number_mut_t n = kth_vm_big_number_construct_from_value(-12345);
    REQUIRE(kth_vm_big_number_sign(n) < 0);
    REQUIRE(kth_vm_big_number_is_negative(n) != 0);
    REQUIRE(kth_vm_big_number_is_nonzero(n) != 0);
    REQUIRE(kth_vm_big_number_to_int32_saturating(n) == -12345);
    kth_vm_big_number_destruct(n);
}

TEST_CASE("C-API BigNumber - construct_from_decimal_str parses base-10",
          "[C-API BigNumber][lifecycle]") {
    kth_big_number_mut_t n = kth_vm_big_number_construct_from_decimal_str("99999999999999999999");
    REQUIRE(n != NULL);
    // Verify via to_string (round-trip through the canonical decimal form).
    char* s = kth_vm_big_number_to_string(n);
    REQUIRE(s != NULL);
    REQUIRE(strcmp(s, "99999999999999999999") == 0);
    kth_core_destruct_string(s);
    kth_vm_big_number_destruct(n);
}

TEST_CASE("C-API BigNumber - copy is independent of source",
          "[C-API BigNumber][lifecycle]") {
    kth_big_number_mut_t a = kth_vm_big_number_construct_from_value(1000);
    kth_big_number_mut_t b = kth_vm_big_number_copy(a);
    REQUIRE(b != NULL);

    // Mutating the copy (via negate) must not bleed back.
    kth_vm_big_number_negate(b);
    REQUIRE(kth_vm_big_number_to_int32_saturating(a) == 1000);
    REQUIRE(kth_vm_big_number_to_int32_saturating(b) == -1000);

    kth_vm_big_number_destruct(b);
    kth_vm_big_number_destruct(a);
}

// ---------------------------------------------------------------------------
// String / hex accessors
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - to_string round-trips through decimal",
          "[C-API BigNumber][encode]") {
    kth_big_number_mut_t n = kth_vm_big_number_construct_from_value(42);
    char* s = kth_vm_big_number_to_string(n);
    REQUIRE(strcmp(s, "42") == 0);
    kth_core_destruct_string(s);
    kth_vm_big_number_destruct(n);
}

TEST_CASE("C-API BigNumber - from_hex / to_hex round-trip",
          "[C-API BigNumber][encode]") {
    kth_big_number_mut_t n = kth_vm_big_number_from_hex("ff00");
    REQUIRE(n != NULL);
    char* s = kth_vm_big_number_to_hex(n);
    REQUIRE(s != NULL);
    // Hex formatter returns canonical form (no leading zeros, no prefix).
    REQUIRE(strcmp(s, "ff00") == 0);
    kth_core_destruct_string(s);
    kth_vm_big_number_destruct(n);
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - compare orders two handles",
          "[C-API BigNumber][compare]") {
    kth_big_number_mut_t a = kth_vm_big_number_construct_from_value(5);
    kth_big_number_mut_t b = kth_vm_big_number_construct_from_value(5);
    kth_big_number_mut_t c = kth_vm_big_number_construct_from_value(10);

    REQUIRE(kth_vm_big_number_equals(a, b) != 0);
    REQUIRE(kth_vm_big_number_equals(a, c) == 0);
    REQUIRE(kth_vm_big_number_compare(a, c) < 0);
    REQUIRE(kth_vm_big_number_compare(c, a) > 0);
    REQUIRE(kth_vm_big_number_compare(a, b) == 0);

    kth_vm_big_number_destruct(c);
    kth_vm_big_number_destruct(b);
    kth_vm_big_number_destruct(a);
}

// ---------------------------------------------------------------------------
// Arithmetic producing new handles
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - pow produces a new handle with correct value",
          "[C-API BigNumber][arithmetic]") {
    // 2^10 = 1024. `pow` returns an owned new handle; inputs untouched.
    kth_big_number_mut_t base = kth_vm_big_number_construct_from_value(2);
    kth_big_number_mut_t exp = kth_vm_big_number_construct_from_value(10);
    kth_big_number_mut_t result = kth_vm_big_number_pow(base, exp);
    REQUIRE(result != NULL);
    REQUIRE(kth_vm_big_number_to_int32_saturating(result) == 1024);

    // Inputs unchanged.
    REQUIRE(kth_vm_big_number_to_int32_saturating(base) == 2);
    REQUIRE(kth_vm_big_number_to_int32_saturating(exp) == 10);

    kth_vm_big_number_destruct(result);
    kth_vm_big_number_destruct(exp);
    kth_vm_big_number_destruct(base);
}

TEST_CASE("C-API BigNumber - add / subtract / multiply operators produce new handles",
          "[C-API BigNumber][arithmetic]") {
    // `operator+ - *` surface is emitted as `add` / `subtract` /
    // `multiply` — functional semantics, inputs untouched. One
    // battery covers all three so the precondition / ownership
    // shape exercises together.
    kth_big_number_mut_t a = kth_vm_big_number_construct_from_value(12345);
    kth_big_number_mut_t b = kth_vm_big_number_construct_from_value(67);

    kth_big_number_mut_t sum = kth_vm_big_number_add(a, b);
    REQUIRE(kth_vm_big_number_to_int32_saturating(sum) == 12345 + 67);

    kth_big_number_mut_t diff = kth_vm_big_number_subtract(a, b);
    REQUIRE(kth_vm_big_number_to_int32_saturating(diff) == 12345 - 67);

    kth_big_number_mut_t product = kth_vm_big_number_multiply(a, b);
    REQUIRE(kth_vm_big_number_to_int32_saturating(product) == 12345 * 67);

    // `a` and `b` still hold their original values.
    REQUIRE(kth_vm_big_number_to_int32_saturating(a) == 12345);
    REQUIRE(kth_vm_big_number_to_int32_saturating(b) == 67);

    kth_vm_big_number_destruct(product);
    kth_vm_big_number_destruct(diff);
    kth_vm_big_number_destruct(sum);
    kth_vm_big_number_destruct(b);
    kth_vm_big_number_destruct(a);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - serialize → deserialize round-trip",
          "[C-API BigNumber][encode]") {
    kth_big_number_mut_t src = kth_vm_big_number_construct_from_value(123456);
    kth_size_t size = 0;
    uint8_t* buf = kth_vm_big_number_serialize(src, &size);
    REQUIRE(buf != NULL);
    REQUIRE(size > 0);

    kth_big_number_mut_t dst = kth_vm_big_number_construct_default();
    REQUIRE(kth_vm_big_number_deserialize(dst, buf, size) != 0);
    REQUIRE(kth_vm_big_number_equals(src, dst) != 0);

    kth_vm_big_number_destruct(dst);
    kth_core_destruct_array(buf);
    kth_vm_big_number_destruct(src);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API BigNumber - sign null aborts",
          "[C-API BigNumber][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_big_number_sign(NULL));
}

TEST_CASE("C-API BigNumber - copy null aborts",
          "[C-API BigNumber][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_big_number_copy(NULL));
}
