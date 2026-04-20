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

#include <kth/capi/primitives.h>
#include <kth/capi/vm/function_table.h>

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Two short distinct keys and two distinct values. Short enough to
// make reasoning about the tests cheap; still covers the byte-buffer
// (ptr, len) contract.
static uint8_t const kKeyA[]   = { 0x01, 0x02 };
static uint8_t const kKeyB[]   = { 0x03, 0x04, 0x05 };
static uint8_t const kValueA[] = { 0xaa };
static uint8_t const kValueB[] = { 0xbb, 0xbc };

// ---------------------------------------------------------------------------
// construct / destruct / count
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - default construct is empty",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    REQUIRE(m != NULL);
    REQUIRE(kth_vm_function_table_count(m) == 0);
    kth_vm_function_table_destruct(m);
}

// ---------------------------------------------------------------------------
// insert / contains / count
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - insert grows the map and makes keys visible",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();

    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));
    REQUIRE(kth_vm_function_table_count(m) == 1);
    REQUIRE(kth_vm_function_table_contains(m, kKeyA, sizeof(kKeyA)) != 0);
    REQUIRE(kth_vm_function_table_contains(m, kKeyB, sizeof(kKeyB)) == 0);

    kth_vm_function_table_insert(m, kKeyB, sizeof(kKeyB),
                                 kValueB, sizeof(kValueB));
    REQUIRE(kth_vm_function_table_count(m) == 2);
    REQUIRE(kth_vm_function_table_contains(m, kKeyB, sizeof(kKeyB)) != 0);

    kth_vm_function_table_destruct(m);
}

TEST_CASE("C-API FunctionTable - insert with the same key overwrites",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();

    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueB, sizeof(kValueB));

    REQUIRE(kth_vm_function_table_count(m) == 1);  // no growth on overwrite

    kth_size_t sz = 0;
    uint8_t* v = kth_vm_function_table_at(m, kKeyA, sizeof(kKeyA), &sz);
    REQUIRE(v != NULL);
    REQUIRE(sz == sizeof(kValueB));
    REQUIRE(memcmp(v, kValueB, sz) == 0);  // second insert wins
    kth_core_destruct_array(v);

    kth_vm_function_table_destruct(m);
}

// ---------------------------------------------------------------------------
// at — present vs absent
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - at returns a fresh owned copy of the value",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));

    kth_size_t sz = 0;
    uint8_t* got = kth_vm_function_table_at(m, kKeyA, sizeof(kKeyA), &sz);
    REQUIRE(got != NULL);
    REQUIRE(sz == sizeof(kValueA));
    REQUIRE(memcmp(got, kValueA, sz) == 0);
    kth_core_destruct_array(got);

    kth_vm_function_table_destruct(m);
}

TEST_CASE("C-API FunctionTable - at on an absent key returns NULL with size 0",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));

    kth_size_t sz = 1234;  // deliberately non-zero to verify it gets reset
    uint8_t* got = kth_vm_function_table_at(m, kKeyB, sizeof(kKeyB), &sz);
    REQUIRE(got == NULL);
    REQUIRE(sz == 0);

    kth_vm_function_table_destruct(m);
}

// ---------------------------------------------------------------------------
// erase
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - erase removes a present key",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));

    REQUIRE(kth_vm_function_table_erase(m, kKeyA, sizeof(kKeyA)) != 0);
    REQUIRE(kth_vm_function_table_count(m) == 0);
    REQUIRE(kth_vm_function_table_contains(m, kKeyA, sizeof(kKeyA)) == 0);

    kth_vm_function_table_destruct(m);
}

TEST_CASE("C-API FunctionTable - erase on an absent key returns 0 without affecting state",
          "[C-API FunctionTable]") {
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));

    REQUIRE(kth_vm_function_table_erase(m, kKeyB, sizeof(kKeyB)) == 0);
    REQUIRE(kth_vm_function_table_count(m) == 1);
    REQUIRE(kth_vm_function_table_contains(m, kKeyA, sizeof(kKeyA)) != 0);

    kth_vm_function_table_destruct(m);
}

// ---------------------------------------------------------------------------
// nth — iteration
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - nth yields every entry exactly once",
          "[C-API FunctionTable]") {
    // `unordered_flat_map` iteration order is implementation-defined,
    // but the set of (key, value) pairs seen across positions
    // [0, count) must exactly equal the inserted set.
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));
    kth_vm_function_table_insert(m, kKeyB, sizeof(kKeyB),
                                 kValueB, sizeof(kValueB));

    int saw_a = 0;
    int saw_b = 0;
    for (kth_size_t i = 0; i < kth_vm_function_table_count(m); ++i) {
        uint8_t* key = NULL;
        uint8_t* val = NULL;
        kth_size_t key_n = 0;
        kth_size_t val_n = 0;
        kth_vm_function_table_nth(m, i, &key, &key_n, &val, &val_n);
        REQUIRE(key != NULL);
        REQUIRE(val != NULL);

        if (key_n == sizeof(kKeyA) && memcmp(key, kKeyA, key_n) == 0) {
            REQUIRE(val_n == sizeof(kValueA));
            REQUIRE(memcmp(val, kValueA, val_n) == 0);
            ++saw_a;
        } else if (key_n == sizeof(kKeyB) && memcmp(key, kKeyB, key_n) == 0) {
            REQUIRE(val_n == sizeof(kValueB));
            REQUIRE(memcmp(val, kValueB, val_n) == 0);
            ++saw_b;
        } else {
            FAIL("nth returned an unexpected key");
        }

        kth_core_destruct_array(key);
        kth_core_destruct_array(val);
    }
    REQUIRE(saw_a == 1);
    REQUIRE(saw_b == 1);

    kth_vm_function_table_destruct(m);
}

TEST_CASE("C-API FunctionTable - nth out of range leaves outputs untouched",
          "[C-API FunctionTable]") {
    // Documented contract: when `index >= count`, `nth` is a no-op and
    // must not touch any of the four output slots. Seed each slot with
    // a sentinel and verify it survives.
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();
    kth_vm_function_table_insert(m, kKeyA, sizeof(kKeyA),
                                 kValueA, sizeof(kValueA));

    uint8_t key_sentinel = 0;
    uint8_t value_sentinel = 0;
    uint8_t* key = &key_sentinel;
    uint8_t* val = &value_sentinel;
    kth_size_t key_n = 77;
    kth_size_t val_n = 88;

    kth_vm_function_table_nth(m, kth_vm_function_table_count(m),
                              &key, &key_n, &val, &val_n);

    REQUIRE(key == &key_sentinel);
    REQUIRE(val == &value_sentinel);
    REQUIRE(key_n == 77);
    REQUIRE(val_n == 88);

    kth_vm_function_table_destruct(m);
}

// ---------------------------------------------------------------------------
// Empty key / value — byte-buffer contract with (NULL, 0)
// ---------------------------------------------------------------------------

TEST_CASE("C-API FunctionTable - empty key and value roundtrip via (NULL, 0)",
          "[C-API FunctionTable]") {
    // The byte-buffer contract allows `(NULL, 0)` as a valid empty
    // buffer; the map must accept it as both key and value, and
    // `at()` must signal "present" on an empty-valued key distinctly
    // from "absent" — the generator guards the empty-value path with
    // a direct 1-byte allocation so `malloc(0)` implementation-
    // defined behaviour doesn't leak through.
    kth_function_table_mut_t m = kth_vm_function_table_construct_default();

    kth_vm_function_table_insert(m, NULL, 0, NULL, 0);
    REQUIRE(kth_vm_function_table_count(m) == 1);
    REQUIRE(kth_vm_function_table_contains(m, NULL, 0) != 0);

    kth_size_t sz = 999;  // deliberately non-zero to verify it gets reset
    uint8_t* v = kth_vm_function_table_at(m, NULL, 0, &sz);
    REQUIRE(v != NULL);
    REQUIRE(sz == 0);
    kth_core_destruct_array(v);

    kth_vm_function_table_destruct(m);
}
