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

#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/point.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static kth_hash_t const kHash = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

static kth_hash_t const kAllOnes = {{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
}};

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - default construct is invalid", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    REQUIRE(kth_chain_output_point_is_valid(op) == 0);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - construct from hash and index", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHash, 1234u);
    REQUIRE(kth_chain_output_point_is_valid(op) != 0);
    REQUIRE(kth_chain_output_point_index(op) == 1234u);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(op), kHash) != 0);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - construct from point preserves fields", "[C-API OutputPoint]") {
    kth_point_mut_t pt = kth_chain_point_construct(&kHash, 42u);
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_point(pt);

    REQUIRE(kth_chain_output_point_is_valid(op) != 0);
    REQUIRE(kth_chain_output_point_index(op) == 42u);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(op), kHash) != 0);

    kth_chain_output_point_destruct(op);
    kth_chain_point_destruct(pt);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - from_data insufficient bytes fails", "[C-API OutputPoint]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    kth_output_point_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_output_point_construct_from_data(data, 10, 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API OutputPoint - to_data / from_data roundtrip", "[C-API OutputPoint]") {
    kth_output_point_mut_t expected =
        kth_chain_output_point_construct_from_hash_index(&kHash, 53213u);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_output_point_to_data(expected, 1, &size);
    REQUIRE(size == 36u);
    REQUIRE(raw != NULL);

    // The factory we just exposed wraps point::from_data through the
    // output_point(point const&) conversion ctor, so the resulting handle
    // is genuinely an output_point and the round-trip preserves the wire
    // bytes (the layout matches because output_point has no extra wire
    // members on top of point).
    kth_output_point_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_output_point_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);

    REQUIRE(kth_chain_output_point_is_valid(parsed) != 0);
    REQUIRE(kth_chain_output_point_index(parsed) == 53213u);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(parsed), kHash) != 0);

    kth_core_destruct_array(raw);
    kth_chain_output_point_destruct(parsed);
    kth_chain_output_point_destruct(expected);
}

TEST_CASE("C-API OutputPoint - construct_from_data NULL data with zero size returns error",
          "[C-API OutputPoint]") {
    // (NULL, 0) is a valid empty input — no abort. The parser will fail
    // because zero bytes are insufficient, but it must do so gracefully.
    kth_output_point_mut_t out = NULL;
    kth_error_code_t ec =
        kth_chain_output_point_construct_from_data(NULL, 0, 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - serialized_size is fixed 36", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHash, 7u);
    REQUIRE(kth_chain_output_point_serialized_size(op, 1) == 36u);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - to_data fixed-size payload", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHash, 7u);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_output_point_to_data(op, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size == 36u);

    // Last 4 bytes are the index in little-endian.
    REQUIRE(raw[32] == 0x07);
    REQUIRE(raw[33] == 0x00);
    REQUIRE(raw[34] == 0x00);
    REQUIRE(raw[35] == 0x00);

    kth_core_destruct_array(raw);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - hash setter roundtrip", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    REQUIRE(kth_hash_is_null(kth_chain_output_point_hash(op)) != 0);

    kth_chain_output_point_set_hash(op, &kHash);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(op), kHash) != 0);

    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - index setter roundtrip", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    REQUIRE(kth_chain_output_point_index(op) != 9999u);
    kth_chain_output_point_set_index(op, 9999u);
    REQUIRE(kth_chain_output_point_index(op) == 9999u);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - copy preserves fields", "[C-API OutputPoint]") {
    kth_output_point_mut_t original = kth_chain_output_point_construct_from_hash_index(&kHash, 524342u);
    kth_output_point_mut_t copy = kth_chain_output_point_copy(original);

    REQUIRE(kth_chain_output_point_is_valid(copy) != 0);
    REQUIRE(kth_chain_output_point_equals(original, copy) != 0);
    REQUIRE(kth_chain_output_point_index(copy) == 524342u);

    kth_chain_output_point_destruct(copy);
    kth_chain_output_point_destruct(original);
}

TEST_CASE("C-API OutputPoint - equals duplicates", "[C-API OutputPoint]") {
    kth_output_point_mut_t a = kth_chain_output_point_construct_from_hash_index(&kHash, 524342u);
    kth_output_point_mut_t b = kth_chain_output_point_construct_from_hash_index(&kHash, 524342u);
    kth_output_point_mut_t c = kth_chain_output_point_construct_default();

    REQUIRE(kth_chain_output_point_equals(a, b) != 0);
    REQUIRE(kth_chain_output_point_equals(a, c) == 0);

    kth_chain_output_point_destruct(a);
    kth_chain_output_point_destruct(b);
    kth_chain_output_point_destruct(c);
}

// ---------------------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - checksum all ones", "[C-API OutputPoint]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kAllOnes, 0xffffffffu);
    REQUIRE(kth_chain_output_point_checksum(op) == 0xffffffffffffffffull);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - checksum all zeros", "[C-API OutputPoint]") {
    kth_hash_t zero;
    memset(zero.hash, 0, sizeof(zero.hash));
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&zero, 0u);
    REQUIRE(kth_chain_output_point_checksum(op) == 0ull);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - satoshi_fixed_size is 36", "[C-API OutputPoint]") {
    REQUIRE(kth_chain_output_point_satoshi_fixed_size() == 36u);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API OutputPoint - construct_from_data null data with non-zero size aborts",
          "[C-API OutputPoint][precondition]") {
    KTH_EXPECT_ABORT({
        kth_output_point_mut_t out = NULL;
        kth_chain_output_point_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API OutputPoint - construct_from_data null out aborts",
          "[C-API OutputPoint][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_output_point_construct_from_data(data, 10, 1, NULL));
}

// Both `kth_chain_output_point_construct_from_hash_index` (safe, takes
// `kth_hash_t const*`) and its `_unsafe` companion (takes raw
// `uint8_t const*`) enforce the same runtime precondition that the hash
// pointer is non-null.
TEST_CASE("C-API OutputPoint - construct_from_hash_index null hash aborts",
          "[C-API OutputPoint][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_point_construct_from_hash_index(NULL, 0));
}

TEST_CASE("C-API OutputPoint - construct_from_hash_index_unsafe null hash aborts",
          "[C-API OutputPoint][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_point_construct_from_hash_index_unsafe(NULL, 0));
}

TEST_CASE("C-API OutputPoint - construct from null point aborts",
          "[C-API OutputPoint][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_point_construct_from_point(NULL));
}

TEST_CASE("C-API OutputPoint - to_data null out_size aborts",
          "[C-API OutputPoint][precondition]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_point_to_data(op, 1, NULL));
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - copy null self aborts",
          "[C-API OutputPoint][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_point_copy(NULL));
}

TEST_CASE("C-API OutputPoint - set_hash null aborts",
          "[C-API OutputPoint][precondition]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_point_set_hash(op, NULL));
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - set_hash_unsafe null aborts",
          "[C-API OutputPoint][precondition]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_point_set_hash_unsafe(op, NULL));
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API OutputPoint - equals null self aborts",
          "[C-API OutputPoint][precondition]") {
    kth_output_point_mut_t other = kth_chain_output_point_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_point_equals(NULL, other));
    kth_chain_output_point_destruct(other);
}
