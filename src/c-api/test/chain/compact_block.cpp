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

#include <kth/capi/chain/compact_block.h>
#include <kth/capi/chain/header.h>
#include <kth/capi/chain/prefilled_transaction.h>
#include <kth/capi/chain/prefilled_transaction_list.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/u64_list.h>

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

// Minimal serializable tx (version=1, no inputs, no outputs, locktime=0).
static uint8_t const kMinimalTx[10] = {
    0x01, 0x00, 0x00, 0x00,
    0x00,
    0x00,
    0x00, 0x00, 0x00, 0x00
};

static kth_header_mut_t make_header(void) {
    return kth_chain_header_construct(
        kHdrVersion, &kPrevHash, &kMerkle, kHdrTimestamp, kHdrBits, kHdrNonce);
}

static kth_transaction_mut_t make_tx(void) {
    kth_transaction_mut_t tx = NULL;
    kth_error_code_t ec = kth_chain_transaction_construct_from_data(
        kMinimalTx, sizeof(kMinimalTx), 1, &tx);
    REQUIRE(ec == kth_ec_success);
    return tx;
}

static kth_u64_list_mut_t make_short_ids(void) {
    kth_u64_list_mut_t list = kth_core_u64_list_construct_default();
    kth_core_u64_list_push_back(list, 0x010203040506ull);
    kth_core_u64_list_push_back(list, 0x0a0b0c0d0e0full);
    return list;
}

static kth_prefilled_transaction_list_mut_t
make_prefilled_list(kth_transaction_const_t tx) {
    kth_prefilled_transaction_list_mut_t list =
        kth_chain_prefilled_transaction_list_construct_default();
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct(0u, tx);
    kth_chain_prefilled_transaction_list_push_back(list, pt);
    kth_chain_prefilled_transaction_destruct(pt);
    return list;
}

static kth_compact_block_mut_t make_fixture(void) {
    kth_header_mut_t header = make_header();
    kth_u64_list_mut_t short_ids = make_short_ids();
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_list_mut_t txs = make_prefilled_list(tx);

    kth_compact_block_mut_t cb = NULL;
    kth_error_code_t ec = kth_chain_compact_block_create(
        header, 0xCAFEBABEu, short_ids, txs, &cb);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(cb != NULL);

    kth_chain_prefilled_transaction_list_destruct(txs);
    kth_chain_transaction_destruct(tx);
    kth_core_u64_list_destruct(short_ids);
    kth_chain_header_destruct(header);
    return cb;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API CompactBlock - create rejects the empty sentinel",
          "[C-API CompactBlock]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    kth_u64_list_mut_t short_ids = kth_core_u64_list_construct_default();
    kth_prefilled_transaction_list_mut_t txs =
        kth_chain_prefilled_transaction_list_construct_default();
    kth_compact_block_mut_t cb = NULL;
    kth_error_code_t ec = kth_chain_compact_block_create(
        header, 0u, short_ids, txs, &cb);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(cb == NULL);
    kth_chain_prefilled_transaction_list_destruct(txs);
    kth_core_u64_list_destruct(short_ids);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API CompactBlock - field constructor preserves fields",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();
    REQUIRE(cb != NULL);
    REQUIRE(kth_chain_compact_block_nonce(cb) == 0xCAFEBABEu);
    kth_chain_compact_block_destruct(cb);
}

TEST_CASE("C-API CompactBlock - destruct null is safe",
          "[C-API CompactBlock]") {
    kth_chain_compact_block_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API CompactBlock - to_data / from_data round-trip",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t original = make_fixture();

    kth_size_t out_size = 0;
    uint8_t* raw =
        kth_chain_compact_block_to_data(original, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size > 0u);

    kth_compact_block_mut_t decoded = NULL;
    kth_error_code_t ec = kth_chain_compact_block_construct_from_data(
        raw, out_size, kProtoVersion, &decoded);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(decoded != NULL);
    REQUIRE(kth_chain_compact_block_equals(original, decoded) != 0);

    kth_core_destruct_array(raw);
    kth_chain_compact_block_destruct(decoded);
    kth_chain_compact_block_destruct(original);
}

TEST_CASE("C-API CompactBlock - serialized_size matches to_data length",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();

    kth_size_t expected =
        kth_chain_compact_block_serialized_size(cb, kProtoVersion);
    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_compact_block_to_data(cb, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == expected);

    kth_core_destruct_array(raw);
    kth_chain_compact_block_destruct(cb);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API CompactBlock - copy preserves equality",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t original = make_fixture();
    kth_compact_block_mut_t copy = kth_chain_compact_block_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_compact_block_equals(original, copy) != 0);
    kth_chain_compact_block_destruct(copy);
    kth_chain_compact_block_destruct(original);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API CompactBlock - header getter returns borrowed view",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();
    kth_header_const_t hdr = kth_chain_compact_block_header(cb);
    REQUIRE(hdr != NULL);
    REQUIRE(kth_chain_header_version(hdr) == kHdrVersion);
    kth_chain_compact_block_destruct(cb);
}

TEST_CASE("C-API CompactBlock - nonce getter reflects input",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();
    REQUIRE(kth_chain_compact_block_nonce(cb) == 0xCAFEBABEu);
    kth_chain_compact_block_destruct(cb);
}

TEST_CASE("C-API CompactBlock - short_ids getter reflects input",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();
    // `kth_chain_compact_block_short_ids` returns an owned
    // `kth_u64_list_mut_t` (the parent doesn't keep it after the call),
    // so the test owns it and must release with
    // `kth_core_u64_list_destruct`.
    kth_u64_list_mut_t ids = kth_chain_compact_block_short_ids(cb);
    REQUIRE(ids != NULL);
    REQUIRE(kth_core_u64_list_count(ids) == 2u);
    REQUIRE(kth_core_u64_list_nth(ids, 0) == 0x010203040506ull);
    REQUIRE(kth_core_u64_list_nth(ids, 1) == 0x0a0b0c0d0e0full);
    kth_core_u64_list_destruct(ids);
    kth_chain_compact_block_destruct(cb);
}

TEST_CASE("C-API CompactBlock - transactions getter reflects input",
          "[C-API CompactBlock]") {
    kth_compact_block_mut_t cb = make_fixture();
    kth_prefilled_transaction_list_const_t view =
        kth_chain_compact_block_transactions(cb);
    REQUIRE(view != NULL);
    REQUIRE(kth_chain_prefilled_transaction_list_count(view) == 1u);
    kth_chain_compact_block_destruct(cb);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API CompactBlock - create null header aborts",
          "[C-API CompactBlock][precondition]") {
    kth_u64_list_mut_t short_ids = make_short_ids();
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_list_mut_t txs = make_prefilled_list(tx);
    kth_compact_block_mut_t out = NULL;
    KTH_EXPECT_ABORT(
        kth_chain_compact_block_create(NULL, 0u, short_ids, txs, &out));
    kth_chain_prefilled_transaction_list_destruct(txs);
    kth_chain_transaction_destruct(tx);
    kth_core_u64_list_destruct(short_ids);
}

TEST_CASE("C-API CompactBlock - create null short_ids aborts",
          "[C-API CompactBlock][precondition]") {
    kth_header_mut_t header = make_header();
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_list_mut_t txs = make_prefilled_list(tx);
    kth_compact_block_mut_t out = NULL;
    KTH_EXPECT_ABORT(
        kth_chain_compact_block_create(header, 0u, NULL, txs, &out));
    kth_chain_prefilled_transaction_list_destruct(txs);
    kth_chain_transaction_destruct(tx);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API CompactBlock - create null transactions aborts",
          "[C-API CompactBlock][precondition]") {
    kth_header_mut_t header = make_header();
    kth_u64_list_mut_t short_ids = make_short_ids();
    kth_compact_block_mut_t out = NULL;
    KTH_EXPECT_ABORT(
        kth_chain_compact_block_create(header, 0u, short_ids, NULL, &out));
    kth_core_u64_list_destruct(short_ids);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API CompactBlock - construct_from_data null data with non-zero size aborts",
          "[C-API CompactBlock][precondition]") {
    KTH_EXPECT_ABORT({
        kth_compact_block_mut_t out = NULL;
        kth_chain_compact_block_construct_from_data(
            NULL, 1, kProtoVersion, &out);
    });
}

TEST_CASE("C-API CompactBlock - construct_from_data null out aborts",
          "[C-API CompactBlock][precondition]") {
    uint8_t data[2] = { 0x00, 0x00 };
    KTH_EXPECT_ABORT(kth_chain_compact_block_construct_from_data(
        data, 2, kProtoVersion, NULL));
}

TEST_CASE("C-API CompactBlock - to_data null out_size aborts",
          "[C-API CompactBlock][precondition]") {
    kth_compact_block_mut_t cb = make_fixture();
    KTH_EXPECT_ABORT(kth_chain_compact_block_to_data(cb, kProtoVersion, NULL));
    kth_chain_compact_block_destruct(cb);
}

TEST_CASE("C-API CompactBlock - copy null aborts",
          "[C-API CompactBlock][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_compact_block_copy(NULL));
}

TEST_CASE("C-API CompactBlock - equals null aborts",
          "[C-API CompactBlock][precondition]") {
    kth_compact_block_mut_t other = make_fixture();
    KTH_EXPECT_ABORT(kth_chain_compact_block_equals(NULL, other));
    kth_chain_compact_block_destruct(other);
}
