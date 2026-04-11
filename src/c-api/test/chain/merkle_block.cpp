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

#include <kth/capi/chain/header.h>
#include <kth/capi/chain/merkle_block.h>
#include <kth/capi/hash.h>
#include <kth/capi/hash_list.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint32_t const kProtoVersion = 70015u;

static uint32_t const kHdrVersion   = 1u;
static uint32_t const kHdrTimestamp = 1296688602u;
static uint32_t const kHdrBits      = 0x1d00ffffu;
static uint32_t const kHdrNonce     = 414098458u;

static kth_hash_t const kPrevHash = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

static kth_hash_t const kMerkle = {{
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a
}};

static kth_hash_t const kHashA = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_hash_t const kHashB = {{
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40
}};

static uint8_t const kFlags[3] = { 0xFF, 0x0F, 0x00 };

static kth_header_mut_t make_header(void) {
    return kth_chain_header_construct(
        kHdrVersion, kPrevHash, kMerkle, kHdrTimestamp, kHdrBits, kHdrNonce);
}

static kth_hash_list_mut_t make_two_hashes(void) {
    kth_hash_list_mut_t list = kth_core_hash_list_construct_default();
    kth_core_hash_list_push_back(list, kHashA);
    kth_core_hash_list_push_back(list, kHashB);
    return list;
}

static kth_merkle_block_mut_t make_fixture(void) {
    kth_header_mut_t header = make_header();
    kth_hash_list_mut_t hashes = make_two_hashes();
    kth_merkle_block_mut_t mb =
        kth_chain_merkle_block_construct_from_header_total_transactions_hashes_flags(
            header, 2u, hashes, kFlags, sizeof(kFlags));
    kth_core_hash_list_destruct(hashes);
    kth_chain_header_destruct(header);
    return mb;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - default construct is invalid",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    REQUIRE(kth_chain_merkle_block_is_valid(mb) == 0);
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - field constructor preserves fields",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();
    REQUIRE(mb != NULL);
    REQUIRE(kth_chain_merkle_block_is_valid(mb) != 0);
    REQUIRE(kth_chain_merkle_block_total_transactions(mb) == 2u);
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - destruct null is safe",
          "[C-API MerkleBlock]") {
    kth_chain_merkle_block_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - to_data / from_data round-trip",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t original = make_fixture();

    kth_size_t out_size = 0;
    uint8_t* raw =
        kth_chain_merkle_block_to_data(original, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size > 0u);

    kth_merkle_block_mut_t decoded = NULL;
    kth_error_code_t ec = kth_chain_merkle_block_construct_from_data(
        raw, out_size, kProtoVersion, &decoded);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(decoded != NULL);
    REQUIRE(kth_chain_merkle_block_is_valid(decoded) != 0);
    REQUIRE(kth_chain_merkle_block_equals(original, decoded) != 0);

    kth_core_destruct_array(raw);
    kth_chain_merkle_block_destruct(decoded);
    kth_chain_merkle_block_destruct(original);
}

TEST_CASE("C-API MerkleBlock - serialized_size matches to_data length",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();

    kth_size_t expected =
        kth_chain_merkle_block_serialized_size(mb, kProtoVersion);
    kth_size_t out_size = 0;
    uint8_t* raw =
        kth_chain_merkle_block_to_data(mb, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == expected);

    kth_core_destruct_array(raw);
    kth_chain_merkle_block_destruct(mb);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - copy preserves equality",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t original = make_fixture();
    kth_merkle_block_mut_t copy = kth_chain_merkle_block_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_merkle_block_equals(original, copy) != 0);
    kth_chain_merkle_block_destruct(copy);
    kth_chain_merkle_block_destruct(original);
}

TEST_CASE("C-API MerkleBlock - equals distinguishes different instances",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t a = make_fixture();
    kth_merkle_block_mut_t b = kth_chain_merkle_block_construct_default();
    REQUIRE(kth_chain_merkle_block_equals(a, b) == 0);
    kth_chain_merkle_block_destruct(a);
    kth_chain_merkle_block_destruct(b);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - header getter returns borrowed view",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();
    kth_header_const_t hdr = kth_chain_merkle_block_header(mb);
    REQUIRE(hdr != NULL);
    REQUIRE(kth_chain_header_version(hdr) == kHdrVersion);
    // Do NOT destruct hdr — it is borrowed.
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - hashes getter returns the two hashes",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();
    kth_hash_list_const_t view = kth_chain_merkle_block_hashes(mb);
    REQUIRE(view != NULL);
    REQUIRE(kth_core_hash_list_count(view) == 2u);

    kth_hash_t first  = kth_core_hash_list_nth(view, 0);
    kth_hash_t second = kth_core_hash_list_nth(view, 1);
    REQUIRE(memcmp(first.hash,  kHashA.hash, KTH_BITCOIN_HASH_SIZE) == 0);
    REQUIRE(memcmp(second.hash, kHashB.hash, KTH_BITCOIN_HASH_SIZE) == 0);

    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - flags round-trip", "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();

    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_merkle_block_flags(mb, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == sizeof(kFlags));
    REQUIRE(memcmp(raw, kFlags, sizeof(kFlags)) == 0);

    kth_core_destruct_array(raw);
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - set_total_transactions updates field",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    kth_chain_merkle_block_set_total_transactions(mb, 42u);
    REQUIRE(kth_chain_merkle_block_total_transactions(mb) == 42u);
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - set_hashes updates list",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    kth_hash_list_mut_t hashes = make_two_hashes();
    kth_chain_merkle_block_set_hashes(mb, hashes);

    kth_hash_list_const_t view = kth_chain_merkle_block_hashes(mb);
    REQUIRE(kth_core_hash_list_count(view) == 2u);

    kth_core_hash_list_destruct(hashes);
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - set_flags updates byte buffer",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    uint8_t const bytes[2] = { 0xAA, 0xBB };
    kth_chain_merkle_block_set_flags(mb, bytes, sizeof(bytes));

    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_merkle_block_flags(mb, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == 2u);
    REQUIRE(raw[0] == 0xAA);
    REQUIRE(raw[1] == 0xBB);

    kth_core_destruct_array(raw);
    kth_chain_merkle_block_destruct(mb);
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - reset clears the object",
          "[C-API MerkleBlock]") {
    kth_merkle_block_mut_t mb = make_fixture();
    kth_chain_merkle_block_reset(mb);
    REQUIRE(kth_chain_merkle_block_is_valid(mb) == 0);
    REQUIRE(kth_chain_merkle_block_total_transactions(mb) == 0u);
    REQUIRE(kth_core_hash_list_count(kth_chain_merkle_block_hashes(mb)) == 0u);

    kth_size_t flags_size = 0;
    uint8_t* flags = kth_chain_merkle_block_flags(mb, &flags_size);
    REQUIRE(flags_size == 0u);
    kth_core_destruct_array(flags);

    kth_chain_merkle_block_destruct(mb);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API MerkleBlock - construct_from_data null data with non-zero size aborts",
          "[C-API MerkleBlock][precondition]") {
    KTH_EXPECT_ABORT({
        kth_merkle_block_mut_t out = NULL;
        kth_chain_merkle_block_construct_from_data(NULL, 1, kProtoVersion, &out);
    });
}

TEST_CASE("C-API MerkleBlock - construct_from_data null out aborts",
          "[C-API MerkleBlock][precondition]") {
    uint8_t data[2] = { 0x00, 0x00 };
    KTH_EXPECT_ABORT(kth_chain_merkle_block_construct_from_data(
        data, 2, kProtoVersion, NULL));
}

TEST_CASE("C-API MerkleBlock - construct_from_data non-null out slot aborts",
          "[C-API MerkleBlock][precondition]") {
    // The precondition is `*out == NULL`: the caller must hand over a
    // fresh slot. Overwriting a slot that already owns a handle would
    // silently leak the previous object, so the contract rejects it.
    uint8_t data[2] = { 0x00, 0x00 };
    kth_merkle_block_mut_t out = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_construct_from_data(
        data, 2, kProtoVersion, &out));
    kth_chain_merkle_block_destruct(out);
}

TEST_CASE("C-API MerkleBlock - construct_from_header null header aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_hash_list_mut_t hashes = make_two_hashes();
    KTH_EXPECT_ABORT(
        kth_chain_merkle_block_construct_from_header_total_transactions_hashes_flags(
            NULL, 2u, hashes, kFlags, sizeof(kFlags)));
    kth_core_hash_list_destruct(hashes);
}

TEST_CASE("C-API MerkleBlock - construct_from_block null block aborts",
          "[C-API MerkleBlock][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_merkle_block_construct_from_block(NULL));
}

TEST_CASE("C-API MerkleBlock - to_data null out_size aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_to_data(mb, kProtoVersion, NULL));
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - copy null aborts",
          "[C-API MerkleBlock][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_merkle_block_copy(NULL));
}

TEST_CASE("C-API MerkleBlock - equals null aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_merkle_block_mut_t other = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_equals(NULL, other));
    kth_chain_merkle_block_destruct(other);
}

TEST_CASE("C-API MerkleBlock - is_valid null aborts",
          "[C-API MerkleBlock][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_merkle_block_is_valid(NULL));
}

TEST_CASE("C-API MerkleBlock - header getter null aborts",
          "[C-API MerkleBlock][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_merkle_block_header(NULL));
}

TEST_CASE("C-API MerkleBlock - set_header null value aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_set_header(mb, NULL));
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - set_hashes null value aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_set_hashes(mb, NULL));
    kth_chain_merkle_block_destruct(mb);
}

TEST_CASE("C-API MerkleBlock - set_flags null with non-zero size aborts",
          "[C-API MerkleBlock][precondition]") {
    kth_merkle_block_mut_t mb = kth_chain_merkle_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_merkle_block_set_flags(mb, NULL, 1));
    kth_chain_merkle_block_destruct(mb);
}
