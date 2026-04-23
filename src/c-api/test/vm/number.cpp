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

#include <kth/capi/primitives.h>
#include <kth/capi/vm/number.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - default construct yields a zero value",
          "[C-API Number][lifecycle]") {
    kth_number_mut_t n = kth_vm_number_construct_default();
    REQUIRE(n != NULL);
    REQUIRE(kth_vm_number_int64(n) == 0);
    REQUIRE(kth_vm_number_int32(n) == 0);
    REQUIRE(kth_vm_number_is_false(n) != 0);
    REQUIRE(kth_vm_number_is_true(n) == 0);
    kth_vm_number_destruct(n);
}

TEST_CASE("C-API Number - destruct(NULL) is a no-op",
          "[C-API Number][lifecycle]") {
    kth_vm_number_destruct(NULL);
}

TEST_CASE("C-API Number - copy is independent of source",
          "[C-API Number][lifecycle]") {
    kth_number_mut_t a = NULL;
    REQUIRE(kth_vm_number_from_int(7, &a) == kth_ec_success);
    kth_number_mut_t b = kth_vm_number_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(kth_vm_number_int64(b) == 7);

    // Mutating the copy must not bleed back into the source.
    kth_vm_number_safe_add_int64(b, 10);
    REQUIRE(kth_vm_number_int64(b) == 17);
    REQUIRE(kth_vm_number_int64(a) == 7);

    kth_vm_number_destruct(b);
    kth_vm_number_destruct(a);
}

// ---------------------------------------------------------------------------
// from_int / getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - from_int preserves the signed 64-bit value",
          "[C-API Number][getter]") {
    kth_number_mut_t n = NULL;
    REQUIRE(kth_vm_number_from_int(-42, &n) == kth_ec_success);
    REQUIRE(n != NULL);
    REQUIRE(kth_vm_number_int64(n) == -42);
    REQUIRE(kth_vm_number_int32(n) == -42);
    REQUIRE(kth_vm_number_is_true(n) != 0);
    REQUIRE(kth_vm_number_is_false(n) == 0);
    kth_vm_number_destruct(n);
}

TEST_CASE("C-API Number - int32 saturates at int32 boundary",
          "[C-API Number][getter]") {
    // from_int enforces the [-2^31 + 1, 2^31 - 1] consensus range;
    // above-range values fail construction. Anything inside that range
    // round-trips exactly through int32().
    kth_number_mut_t n = NULL;
    REQUIRE(kth_vm_number_from_int(2147483647, &n) == kth_ec_success);
    REQUIRE(kth_vm_number_int32(n) == 2147483647);
    REQUIRE(kth_vm_number_int64(n) == 2147483647);
    kth_vm_number_destruct(n);
}

// ---------------------------------------------------------------------------
// Comparison predicates
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - comparison against int64",
          "[C-API Number][compare]") {
    kth_number_mut_t n = NULL;
    REQUIRE(kth_vm_number_from_int(10, &n) == kth_ec_success);

    REQUIRE(kth_vm_number_greater(n, 5) != 0);
    REQUIRE(kth_vm_number_greater(n, 10) == 0);
    REQUIRE(kth_vm_number_less(n, 20) != 0);
    REQUIRE(kth_vm_number_less_or_equal(n, 10) != 0);
    REQUIRE(kth_vm_number_greater_or_equal(n, 10) != 0);

    kth_vm_number_destruct(n);
}

TEST_CASE("C-API Number - equals compares two handles",
          "[C-API Number][compare]") {
    kth_number_mut_t a = NULL;
    kth_number_mut_t b = NULL;
    REQUIRE(kth_vm_number_from_int(3, &a) == kth_ec_success);
    REQUIRE(kth_vm_number_from_int(3, &b) == kth_ec_success);
    REQUIRE(kth_vm_number_equals(a, b) != 0);

    kth_number_mut_t c = NULL;
    REQUIRE(kth_vm_number_from_int(4, &c) == kth_ec_success);
    REQUIRE(kth_vm_number_equals(a, c) == 0);

    kth_vm_number_destruct(c);
    kth_vm_number_destruct(b);
    kth_vm_number_destruct(a);
}

// ---------------------------------------------------------------------------
// safe_add / safe_sub / safe_mul — in-place (member) variants
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - safe_add_int64 accumulates on the receiver",
          "[C-API Number][arithmetic]") {
    kth_number_mut_t n = NULL;
    REQUIRE(kth_vm_number_from_int(10, &n) == kth_ec_success);
    REQUIRE(kth_vm_number_safe_add_int64(n, 20) != 0);
    REQUIRE(kth_vm_number_int64(n) == 30);
    kth_vm_number_destruct(n);
}

TEST_CASE("C-API Number - safe_sub_number subtracts by handle",
          "[C-API Number][arithmetic]") {
    kth_number_mut_t a = NULL;
    kth_number_mut_t b = NULL;
    REQUIRE(kth_vm_number_from_int(100, &a) == kth_ec_success);
    REQUIRE(kth_vm_number_from_int(30, &b) == kth_ec_success);
    REQUIRE(kth_vm_number_safe_sub_number(a, b) != 0);
    REQUIRE(kth_vm_number_int64(a) == 70);
    kth_vm_number_destruct(b);
    kth_vm_number_destruct(a);
}

// ---------------------------------------------------------------------------
// safe_add_number2 / safe_sub_number2 / safe_mul_number2 — static variants
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - safe_add_number2 produces a new handle",
          "[C-API Number][arithmetic]") {
    // The `_number2` suffix is the arity-disambiguated name for the
    // static two-argument overload. It never mutates the inputs —
    // returns an owned handle via `out`.
    kth_number_mut_t a = NULL;
    kth_number_mut_t b = NULL;
    kth_number_mut_t sum = NULL;
    REQUIRE(kth_vm_number_from_int(3, &a) == kth_ec_success);
    REQUIRE(kth_vm_number_from_int(4, &b) == kth_ec_success);
    REQUIRE(kth_vm_number_safe_add_number2(a, b, &sum) == kth_ec_success);
    REQUIRE(sum != NULL);
    REQUIRE(kth_vm_number_int64(sum) == 7);

    // Inputs untouched.
    REQUIRE(kth_vm_number_int64(a) == 3);
    REQUIRE(kth_vm_number_int64(b) == 4);

    kth_vm_number_destruct(sum);
    kth_vm_number_destruct(b);
    kth_vm_number_destruct(a);
}

// ---------------------------------------------------------------------------
// data round-trip (LSB-first byte encoding)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - data() encodes the int64 as script bytes",
          "[C-API Number][data]") {
    kth_number_mut_t n = NULL;
    REQUIRE(kth_vm_number_from_int(1, &n) == kth_ec_success);

    kth_size_t size = 0;
    uint8_t* bytes = kth_vm_number_data(n, &size);
    REQUIRE(bytes != NULL);
    REQUIRE(size == 1);
    REQUIRE(bytes[0] == 0x01);
    kth_core_destruct_array(bytes);

    kth_vm_number_destruct(n);
}

TEST_CASE("C-API Number - set_data parses a script-encoded byte string",
          "[C-API Number][data]") {
    kth_number_mut_t n = kth_vm_number_construct_default();
    uint8_t const encoded[] = { 0x0a };
    REQUIRE(kth_vm_number_set_data(n, encoded, sizeof(encoded), 4) != 0);
    REQUIRE(kth_vm_number_int64(n) == 10);
    kth_vm_number_destruct(n);
}

// ---------------------------------------------------------------------------
// minimally_encode — in-place byte-buffer transform
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - minimally_encode trims an over-padded encoding",
          "[C-API Number][data]") {
    // `[0x05, 0x00]` is a non-minimal encoding for the integer 5
    // (the trailing zero byte is redundant). `minimally_encode` must
    // strip it and report the trimmed length. The probe-size pattern
    // lets callers allocate exactly: first call with n=0 (NULL) to
    // learn the required size, then again with a fitted buffer.
    uint8_t src[2] = { 0x05, 0x00 };
    // Probe — the input is read-only semantically (the buffer is not
    // touched when n is below required).
    kth_size_t required = kth_vm_number_minimally_encode(src, sizeof(src));
    REQUIRE(required == 1);

    // `src[0]` is overwritten in place with the trimmed encoding; the
    // trailing byte past `required` is left as-is (it's stale).
    REQUIRE(src[0] == 0x05);
}

TEST_CASE("C-API Number - minimally_encode leaves already-minimal input intact",
          "[C-API Number][data]") {
    // [0x05] is already minimal — the function must return 1 and
    // leave the byte untouched.
    uint8_t buf[1] = { 0x05 };
    kth_size_t required = kth_vm_number_minimally_encode(buf, sizeof(buf));
    REQUIRE(required == 1);
    REQUIRE(buf[0] == 0x05);
}

TEST_CASE("C-API Number - minimally_encode on empty input returns 0",
          "[C-API Number][data]") {
    // The empty encoding represents the stack value 0; it is already
    // minimal, so nothing to strip.
    kth_size_t required = kth_vm_number_minimally_encode(NULL, 0);
    REQUIRE(required == 0);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API Number - int64 null aborts",
          "[C-API Number][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_number_int64(NULL));
}

TEST_CASE("C-API Number - copy null aborts",
          "[C-API Number][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_number_copy(NULL));
}
