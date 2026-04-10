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

#include <kth/capi/chain/block.h>
#include <kth/capi/chain/header.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/chain/transaction_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Helpers — use genesis_mainnet as the canonical test block.
// ---------------------------------------------------------------------------

static kth_block_mut_t make_block(void) {
    kth_block_mut_t blk = kth_chain_block_genesis_mainnet();
    REQUIRE(blk != NULL);
    return blk;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - default construct is invalid",
          "[C-API Block]") {
    kth_block_mut_t blk = kth_chain_block_construct_default();
    REQUIRE(kth_chain_block_is_valid(blk) == 0);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - genesis mainnet is valid",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    REQUIRE(kth_chain_block_is_valid(blk) != 0);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - destruct null is safe",
          "[C-API Block]") {
    kth_chain_block_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - from_data insufficient bytes fails",
          "[C-API Block]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    kth_block_mut_t blk = NULL;
    kth_error_code_t ec = kth_chain_block_construct_from_data(
        data, sizeof(data), 1, &blk);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(blk == NULL);
}

TEST_CASE("C-API Block - to_data / from_data roundtrip",
          "[C-API Block]") {
    kth_block_mut_t expected = make_block();

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_block_to_data_simple(expected, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size > 0);

    kth_block_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_block_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_block_is_valid(parsed) != 0);
    REQUIRE(kth_chain_block_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_block_destruct(parsed);
    kth_chain_block_destruct(expected);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - header returns borrowed view",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    kth_header_const_t hdr = kth_chain_block_header(blk);
    REQUIRE(hdr != NULL);
    REQUIRE(kth_chain_header_version(hdr) != 0);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - transactions returns borrowed view",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    kth_transaction_list_const_t txs = kth_chain_block_transactions(blk);
    REQUIRE(txs != NULL);
    // Genesis block has exactly 1 transaction.
    REQUIRE(kth_chain_transaction_list_count(txs) == 1u);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - hash is stable across calls",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    kth_hash_t a = kth_chain_block_hash(blk);
    kth_hash_t b = kth_chain_block_hash(blk);
    REQUIRE(memcmp(a.hash, b.hash, sizeof(a.hash)) == 0);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - serialized_size matches to_data length",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    kth_size_t size = 0;
    uint8_t* raw = kth_chain_block_to_data_simple(blk, &size);
    REQUIRE(raw != NULL);
    REQUIRE(kth_chain_block_serialized_size(blk) == size);
    kth_core_destruct_array(raw);
    kth_chain_block_destruct(blk);
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - genesis has valid merkle root",
          "[C-API Block]") {
    kth_block_mut_t blk = make_block();
    REQUIRE(kth_chain_block_is_valid_merkle_root(blk) != 0);
    kth_chain_block_destruct(blk);
}

TEST_CASE("C-API Block - default is not extra coinbases",
          "[C-API Block]") {
    kth_block_mut_t blk = kth_chain_block_construct_default();
    REQUIRE(kth_chain_block_is_extra_coinbases(blk) == 0);
    kth_chain_block_destruct(blk);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - copy preserves fields",
          "[C-API Block]") {
    kth_block_mut_t original = make_block();
    kth_block_mut_t copy = kth_chain_block_copy(original);

    REQUIRE(kth_chain_block_is_valid(copy) != 0);
    REQUIRE(kth_chain_block_equals(original, copy) != 0);

    kth_chain_block_destruct(copy);
    kth_chain_block_destruct(original);
}

TEST_CASE("C-API Block - equals identical blocks is true, different is false",
          "[C-API Block]") {
    kth_block_mut_t a = make_block();
    kth_block_mut_t b = make_block();
    kth_block_mut_t c = kth_chain_block_construct_default();

    REQUIRE(kth_chain_block_equals(a, b) != 0);
    REQUIRE(kth_chain_block_equals(a, c) == 0);

    kth_chain_block_destruct(a);
    kth_chain_block_destruct(b);
    kth_chain_block_destruct(c);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - construct_from_data null data with non-zero size aborts",
          "[C-API Block][precondition]") {
    KTH_EXPECT_ABORT({
        kth_block_mut_t out = NULL;
        kth_chain_block_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API Block - construct_from_data null out aborts",
          "[C-API Block][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_block_construct_from_data(data, 10, 1, NULL));
}

TEST_CASE("C-API Block - copy null self aborts",
          "[C-API Block][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_block_copy(NULL));
}

TEST_CASE("C-API Block - to_data_simple null out_size aborts",
          "[C-API Block][precondition]") {
    kth_block_mut_t blk = kth_chain_block_construct_default();
    KTH_EXPECT_ABORT(kth_chain_block_to_data_simple(blk, NULL));
    kth_chain_block_destruct(blk);
}
