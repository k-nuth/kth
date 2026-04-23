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

#include <kth/capi/data_stack.h>
#include <kth/capi/primitives.h>

#include "test_helpers.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API DataStack - default construct yields empty list",
          "[C-API DataStack][lifecycle]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_core_data_stack_count(list) == 0);
    kth_core_data_stack_destruct(list);
}

TEST_CASE("C-API DataStack - destruct(NULL) is a no-op",
          "[C-API DataStack][lifecycle]") {
    kth_core_data_stack_destruct(NULL);
}

TEST_CASE("C-API DataStack - copy is independent of source",
          "[C-API DataStack][lifecycle]") {
    kth_data_stack_mut_t src = kth_core_data_stack_construct_default();
    uint8_t const a[] = { 0x01, 0x02, 0x03 };
    uint8_t const b[] = { 0xff };
    kth_core_data_stack_push_back(src, a, sizeof(a));
    kth_core_data_stack_push_back(src, b, sizeof(b));

    kth_data_stack_mut_t cp = kth_core_data_stack_copy(src);
    REQUIRE(cp != NULL);
    REQUIRE(kth_core_data_stack_count(cp) == 2);

    // Mutating the copy must not bleed back into the source.
    kth_core_data_stack_erase(cp, 0);
    REQUIRE(kth_core_data_stack_count(cp) == 1);
    REQUIRE(kth_core_data_stack_count(src) == 2);

    kth_core_data_stack_destruct(cp);
    kth_core_data_stack_destruct(src);
}

// ---------------------------------------------------------------------------
// push_back / count / nth (borrowed view)
// ---------------------------------------------------------------------------

TEST_CASE("C-API DataStack - push_back appends variable-length buffers",
          "[C-API DataStack][push]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    uint8_t const b0[] = { 0xde, 0xad, 0xbe, 0xef };
    uint8_t const b1[] = { 0x42 };
    kth_core_data_stack_push_back(list, b0, sizeof(b0));
    kth_core_data_stack_push_back(list, b1, sizeof(b1));
    REQUIRE(kth_core_data_stack_count(list) == 2);

    kth_size_t size = 0;
    uint8_t const* p0 = kth_core_data_stack_nth(list, 0, &size);
    REQUIRE(size == sizeof(b0));
    REQUIRE(memcmp(p0, b0, sizeof(b0)) == 0);

    uint8_t const* p1 = kth_core_data_stack_nth(list, 1, &size);
    REQUIRE(size == sizeof(b1));
    REQUIRE(p1[0] == 0x42);

    kth_core_data_stack_destruct(list);
}

TEST_CASE("C-API DataStack - push_back handles empty elements (NULL + n=0)",
          "[C-API DataStack][push]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    kth_core_data_stack_push_back(list, NULL, 0);
    REQUIRE(kth_core_data_stack_count(list) == 1);

    // `nth` may return NULL for an empty element (implementation-defined
    // on some platforms for `std::vector::data()`). Callers must check
    // `*out_size`, not the pointer, before dereferencing.
    kth_size_t size = 0xdead;
    uint8_t const* p = kth_core_data_stack_nth(list, 0, &size);
    (void)p;
    REQUIRE(size == 0);

    kth_core_data_stack_destruct(list);
}

// ---------------------------------------------------------------------------
// nth_copy — owned bytes the caller must free
// ---------------------------------------------------------------------------

TEST_CASE("C-API DataStack - nth_copy hands back an owned buffer",
          "[C-API DataStack][copy]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    uint8_t const bytes[] = { 0x10, 0x20, 0x30 };
    kth_core_data_stack_push_back(list, bytes, sizeof(bytes));

    kth_size_t size = 0;
    uint8_t* owned = kth_core_data_stack_nth_copy(list, 0, &size);
    REQUIRE(owned != NULL);
    REQUIRE(size == sizeof(bytes));
    REQUIRE(memcmp(owned, bytes, sizeof(bytes)) == 0);

    // The owned buffer survives list destruction.
    kth_core_data_stack_destruct(list);
    REQUIRE(owned[0] == 0x10);
    kth_core_destruct_array(owned);
}

// ---------------------------------------------------------------------------
// erase
// ---------------------------------------------------------------------------

TEST_CASE("C-API DataStack - erase removes the selected element",
          "[C-API DataStack][erase]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    uint8_t const b0[] = { 0xaa };
    uint8_t const b1[] = { 0xbb };
    uint8_t const b2[] = { 0xcc };
    kth_core_data_stack_push_back(list, b0, 1);
    kth_core_data_stack_push_back(list, b1, 1);
    kth_core_data_stack_push_back(list, b2, 1);

    kth_core_data_stack_erase(list, 1);
    REQUIRE(kth_core_data_stack_count(list) == 2);

    kth_size_t size = 0;
    uint8_t const* p0 = kth_core_data_stack_nth(list, 0, &size);
    REQUIRE(p0[0] == 0xaa);
    uint8_t const* p1 = kth_core_data_stack_nth(list, 1, &size);
    REQUIRE(p1[0] == 0xcc);

    kth_core_data_stack_destruct(list);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API DataStack - count null aborts",
          "[C-API DataStack][precondition]") {
    KTH_EXPECT_ABORT(kth_core_data_stack_count(NULL));
}

TEST_CASE("C-API DataStack - push_back null aborts",
          "[C-API DataStack][precondition]") {
    uint8_t const buf[] = { 0x01 };
    KTH_EXPECT_ABORT(kth_core_data_stack_push_back(NULL, buf, 1));
}

TEST_CASE("C-API DataStack - nth out-of-range aborts",
          "[C-API DataStack][precondition]") {
    kth_data_stack_mut_t list = kth_core_data_stack_construct_default();
    kth_size_t size = 0;
    KTH_EXPECT_ABORT(kth_core_data_stack_nth(list, 5, &size));
    kth_core_data_stack_destruct(list);
}
