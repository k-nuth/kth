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

#include <kth/capi/chain/point.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint8_t const kHash[32] = {
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t const kAllOnes[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static kth_hash_t make_hash(uint8_t const* bytes) {
    kth_hash_t h;
    memcpy(h.hash, bytes, KTH_BITCOIN_HASH_SIZE);
    return h;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - default construct is invalid", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_construct_default();
    REQUIRE(kth_chain_point_is_valid(point) == 0);
    kth_chain_point_destruct(point);
}

TEST_CASE("C-API Point - field constructor preserves hash and index", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_construct(kHash, 1234u);
    REQUIRE(kth_chain_point_is_valid(point) != 0);
    REQUIRE(kth_chain_point_index(point) == 1234u);
    REQUIRE(kth_hash_equal(kth_chain_point_hash(point), make_hash(kHash)) != 0);
    kth_chain_point_destruct(point);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - from_data insufficient bytes fails", "[C-API Point]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    kth_point_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_point_construct_from_data(data, 10, 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Point - to_data / from_data roundtrip", "[C-API Point]") {
    kth_point_mut_t expected = kth_chain_point_construct(kHash, 53213u);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_point_to_data(expected, 1, &size);
    REQUIRE(size == 36u);
    REQUIRE(raw != NULL);

    kth_point_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_point_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);

    REQUIRE(kth_chain_point_is_valid(parsed) != 0);
    REQUIRE(kth_chain_point_equals(expected, parsed) != 0);
    REQUIRE(kth_chain_point_index(parsed) == 53213u);
    REQUIRE(kth_hash_equal(kth_chain_point_hash(parsed), make_hash(kHash)) != 0);

    kth_core_destruct_array(raw);
    kth_chain_point_destruct(parsed);
    kth_chain_point_destruct(expected);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - hash setter roundtrip", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_construct_default();
    REQUIRE(kth_hash_is_null(kth_chain_point_hash(point)) != 0);

    kth_chain_point_set_hash(point, kHash);
    REQUIRE(kth_hash_equal(kth_chain_point_hash(point), make_hash(kHash)) != 0);

    kth_chain_point_destruct(point);
}

TEST_CASE("C-API Point - index setter roundtrip", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_construct_default();
    REQUIRE(kth_chain_point_index(point) != 1254u);
    kth_chain_point_set_index(point, 1254u);
    REQUIRE(kth_chain_point_index(point) == 1254u);
    kth_chain_point_destruct(point);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - copy preserves fields", "[C-API Point]") {
    kth_point_mut_t original = kth_chain_point_construct(kHash, 524342u);
    kth_point_mut_t copy = kth_chain_point_copy(original);

    REQUIRE(kth_chain_point_is_valid(copy) != 0);
    REQUIRE(kth_chain_point_equals(original, copy) != 0);
    REQUIRE(kth_chain_point_index(copy) == 524342u);

    kth_chain_point_destruct(copy);
    kth_chain_point_destruct(original);
}

TEST_CASE("C-API Point - equals duplicates", "[C-API Point]") {
    kth_point_mut_t a = kth_chain_point_construct(kHash, 524342u);
    kth_point_mut_t b = kth_chain_point_construct(kHash, 524342u);
    kth_point_mut_t c = kth_chain_point_construct_default();

    REQUIRE(kth_chain_point_equals(a, b) != 0);
    REQUIRE(kth_chain_point_equals(a, c) == 0);

    kth_chain_point_destruct(a);
    kth_chain_point_destruct(b);
    kth_chain_point_destruct(c);
}

// ---------------------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - checksum all ones returns all ones", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_construct(kAllOnes, 0xffffffffu);
    REQUIRE(kth_chain_point_checksum(point) == 0xffffffffffffffffull);
    kth_chain_point_destruct(point);
}

TEST_CASE("C-API Point - checksum all zeros returns zero", "[C-API Point]") {
    uint8_t zero[32];
    memset(zero, 0, sizeof(zero));
    kth_point_mut_t point = kth_chain_point_construct(zero, 0u);
    REQUIRE(kth_chain_point_checksum(point) == 0ull);
    kth_chain_point_destruct(point);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - satoshi_fixed_size is 36", "[C-API Point]") {
    REQUIRE(kth_chain_point_satoshi_fixed_size() == 36u);
}

TEST_CASE("C-API Point - null factory returns is_null", "[C-API Point]") {
    kth_point_mut_t point = kth_chain_point_null();
    REQUIRE(kth_chain_point_is_null(point) != 0);
    kth_chain_point_destruct(point);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Point - construct_from_data null data aborts",
          "[C-API Point][precondition]") {
    KTH_EXPECT_ABORT({
        kth_point_mut_t out = NULL;
        kth_chain_point_construct_from_data(NULL, 0, 1, &out);
    });
}

TEST_CASE("C-API Point - construct_from_data null out aborts",
          "[C-API Point][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_point_construct_from_data(data, 10, 1, NULL));
}

TEST_CASE("C-API Point - construct null hash aborts",
          "[C-API Point][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_point_construct(NULL, 0));
}

TEST_CASE("C-API Point - to_data null out_size aborts",
          "[C-API Point][precondition]") {
    kth_point_mut_t point = kth_chain_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_point_to_data(point, 1, NULL));
    kth_chain_point_destruct(point);
}

TEST_CASE("C-API Point - copy null self aborts",
          "[C-API Point][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_point_copy(NULL));
}

TEST_CASE("C-API Point - set_hash null aborts",
          "[C-API Point][precondition]") {
    kth_point_mut_t point = kth_chain_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_point_set_hash(point, NULL));
    kth_chain_point_destruct(point);
}

TEST_CASE("C-API Point - equals null self aborts",
          "[C-API Point][precondition]") {
    kth_point_mut_t other = kth_chain_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_point_equals(NULL, other));
    kth_chain_point_destruct(other);
}
