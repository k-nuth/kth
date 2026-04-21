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

#include <kth/capi/chain/get_blocks.h>
#include <kth/capi/hash.h>
#include <kth/capi/hash_list.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Bitcoin protocol version. Any value above
// `kth::domain::message::version::level::minimum` (31402) is fine — the
// wire format is stable for the fields get_blocks carries.
static uint32_t const kProtoVersion = 70015u;

static kth_hash_t const kHashA = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

static kth_hash_t const kHashB = {{
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a
}};

static kth_hash_t const kStopHash = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_hash_list_mut_t make_hash_list_of_two(void) {
    kth_hash_list_mut_t list = kth_core_hash_list_construct_default();
    kth_core_hash_list_push_back(list, kHashA);
    kth_core_hash_list_push_back(list, kHashB);
    return list;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - default construct is invalid", "[C-API GetBlocks]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    REQUIRE(kth_chain_get_blocks_is_valid(gb) == 0);
    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - field constructor is valid", "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct(starts, &kStopHash);
    REQUIRE(gb != NULL);
    REQUIRE(kth_chain_get_blocks_is_valid(gb) != 0);
    kth_chain_get_blocks_destruct(gb);
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - construct_unsafe matches safe variant",
          "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();

    kth_get_blocks_mut_t safe =
        kth_chain_get_blocks_construct(starts, &kStopHash);
    kth_get_blocks_mut_t unsafe =
        kth_chain_get_blocks_construct_unsafe(starts, kStopHash.hash);

    REQUIRE(kth_chain_get_blocks_equals(safe, unsafe) != 0);

    kth_chain_get_blocks_destruct(safe);
    kth_chain_get_blocks_destruct(unsafe);
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - destruct null is safe", "[C-API GetBlocks]") {
    kth_chain_get_blocks_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - to_data / from_data round-trip",
          "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t original =
        kth_chain_get_blocks_construct(starts, &kStopHash);

    kth_size_t out_size = 0;
    uint8_t* raw =
        kth_chain_get_blocks_to_data(original, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size > 0u);

    kth_get_blocks_mut_t decoded = NULL;
    kth_error_code_t ec = kth_chain_get_blocks_construct_from_data(
        raw, out_size, kProtoVersion, &decoded);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(decoded != NULL);
    REQUIRE(kth_chain_get_blocks_is_valid(decoded) != 0);

    REQUIRE(kth_chain_get_blocks_equals(original, decoded) != 0);

    kth_core_destruct_array(raw);
    kth_chain_get_blocks_destruct(decoded);
    kth_chain_get_blocks_destruct(original);
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - serialized_size matches to_data length",
          "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t gb =
        kth_chain_get_blocks_construct(starts, &kStopHash);

    kth_size_t expected =
        kth_chain_get_blocks_serialized_size(gb, kProtoVersion);

    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_get_blocks_to_data(gb, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == expected);

    kth_core_destruct_array(raw);
    kth_chain_get_blocks_destruct(gb);
    kth_core_hash_list_destruct(starts);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - copy preserves equality", "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t original =
        kth_chain_get_blocks_construct(starts, &kStopHash);

    kth_get_blocks_mut_t copy = kth_chain_get_blocks_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_get_blocks_equals(original, copy) != 0);

    kth_chain_get_blocks_destruct(copy);
    kth_chain_get_blocks_destruct(original);
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - equals distinguishes different instances",
          "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();

    kth_get_blocks_mut_t a =
        kth_chain_get_blocks_construct(starts, &kStopHash);
    kth_get_blocks_mut_t b = kth_chain_get_blocks_construct_default();

    REQUIRE(kth_chain_get_blocks_equals(a, b) == 0);

    kth_chain_get_blocks_destruct(a);
    kth_chain_get_blocks_destruct(b);
    kth_core_hash_list_destruct(starts);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - stop_hash round-trips by value",
          "[C-API GetBlocks]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    kth_chain_get_blocks_set_stop_hash(gb, &kStopHash);

    kth_hash_t got = kth_chain_get_blocks_stop_hash(gb);
    REQUIRE(memcmp(got.hash, kStopHash.hash, KTH_BITCOIN_HASH_SIZE) == 0);

    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - set_stop_hash_unsafe accepts raw pointer",
          "[C-API GetBlocks]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    kth_chain_get_blocks_set_stop_hash_unsafe(gb, kStopHash.hash);

    kth_hash_t got = kth_chain_get_blocks_stop_hash(gb);
    REQUIRE(memcmp(got.hash, kStopHash.hash, KTH_BITCOIN_HASH_SIZE) == 0);

    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - start_hashes count reflects input list",
          "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    kth_chain_get_blocks_set_start_hashes(gb, starts);

    kth_hash_list_const_t view = kth_chain_get_blocks_start_hashes(gb);
    REQUIRE(view != NULL);
    REQUIRE(kth_core_hash_list_count(view) == 2u);

    kth_hash_t first  = kth_core_hash_list_nth(view, 0);
    kth_hash_t second = kth_core_hash_list_nth(view, 1);
    REQUIRE(memcmp(first.hash,  kHashA.hash, KTH_BITCOIN_HASH_SIZE) == 0);
    REQUIRE(memcmp(second.hash, kHashB.hash, KTH_BITCOIN_HASH_SIZE) == 0);

    kth_chain_get_blocks_destruct(gb);
    kth_core_hash_list_destruct(starts);
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - reset clears the object", "[C-API GetBlocks]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    kth_get_blocks_mut_t gb =
        kth_chain_get_blocks_construct(starts, &kStopHash);

    kth_chain_get_blocks_reset(gb);

    kth_hash_list_const_t view = kth_chain_get_blocks_start_hashes(gb);
    REQUIRE(kth_core_hash_list_count(view) == 0u);

    kth_chain_get_blocks_destruct(gb);
    kth_core_hash_list_destruct(starts);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API GetBlocks - construct null start hashes aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_get_blocks_construct(NULL, &kStopHash));
}

TEST_CASE("C-API GetBlocks - construct null stop aborts",
          "[C-API GetBlocks][precondition]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_construct(starts, NULL));
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - construct_unsafe null stop aborts",
          "[C-API GetBlocks][precondition]") {
    kth_hash_list_mut_t starts = make_hash_list_of_two();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_construct_unsafe(starts, NULL));
    kth_core_hash_list_destruct(starts);
}

TEST_CASE("C-API GetBlocks - construct_from_data null data with non-zero size aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT({
        kth_get_blocks_mut_t out = NULL;
        kth_chain_get_blocks_construct_from_data(NULL, 1, kProtoVersion, &out);
    });
}

TEST_CASE("C-API GetBlocks - construct_from_data null out aborts",
          "[C-API GetBlocks][precondition]") {
    uint8_t data[2] = { 0x00, 0x00 };
    KTH_EXPECT_ABORT(kth_chain_get_blocks_construct_from_data(
        data, 2, kProtoVersion, NULL));
}

TEST_CASE("C-API GetBlocks - to_data null out_size aborts",
          "[C-API GetBlocks][precondition]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_to_data(gb, kProtoVersion, NULL));
    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - copy null self aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_get_blocks_copy(NULL));
}

TEST_CASE("C-API GetBlocks - equals null self aborts",
          "[C-API GetBlocks][precondition]") {
    kth_get_blocks_mut_t other = kth_chain_get_blocks_construct_default();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_equals(NULL, other));
    kth_chain_get_blocks_destruct(other);
}

TEST_CASE("C-API GetBlocks - is_valid null aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_get_blocks_is_valid(NULL));
}

TEST_CASE("C-API GetBlocks - start_hashes null aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_get_blocks_start_hashes(NULL));
}

TEST_CASE("C-API GetBlocks - stop_hash null aborts",
          "[C-API GetBlocks][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_get_blocks_stop_hash(NULL));
}

TEST_CASE("C-API GetBlocks - set_start_hashes null value aborts",
          "[C-API GetBlocks][precondition]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_set_start_hashes(gb, NULL));
    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - set_stop_hash null aborts",
          "[C-API GetBlocks][precondition]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_set_stop_hash(gb, NULL));
    kth_chain_get_blocks_destruct(gb);
}

TEST_CASE("C-API GetBlocks - set_stop_hash_unsafe null aborts",
          "[C-API GetBlocks][precondition]") {
    kth_get_blocks_mut_t gb = kth_chain_get_blocks_construct_default();
    KTH_EXPECT_ABORT(kth_chain_get_blocks_set_stop_hash_unsafe(gb, NULL));
    kth_chain_get_blocks_destruct(gb);
}
