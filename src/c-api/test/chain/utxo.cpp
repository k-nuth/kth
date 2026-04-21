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

#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/token_capability.h>
#include <kth/capi/chain/token_data.h>
#include <kth/capi/chain/utxo.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static kth_hash_t const kTxid = {{
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
}};

static kth_hash_t const kCategory = {{
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb
}};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Utxo - destruct null is safe", "[C-API Utxo]") {
    kth_chain_utxo_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Utxo - construct_default builds a handle with zero fields",
          "[C-API Utxo]") {
    kth_utxo_mut_t u = kth_chain_utxo_construct_default();
    REQUIRE(kth_chain_utxo_height(u) == 0u);
    REQUIRE(kth_chain_utxo_amount(u) == 0u);
    REQUIRE(kth_chain_utxo_token_data(u) == NULL);

    kth_chain_utxo_destruct(u);
}

TEST_CASE("C-API Utxo - construct from point+amount yields engaged values",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 7u);
    kth_utxo_mut_t u = kth_chain_utxo_construct(op, 5000u, NULL);

    REQUIRE(kth_chain_utxo_amount(u) == 5000u);
    REQUIRE(kth_chain_utxo_token_data(u) == NULL);
    REQUIRE(kth_chain_output_point_index(kth_chain_utxo_point(u)) == 7u);

    kth_chain_utxo_destruct(u);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Utxo - construct with token_data stores it",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 0u);
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategory, 1234u);
    kth_utxo_mut_t u = kth_chain_utxo_construct(op, 1000u, td);

    kth_token_data_const_t got = kth_chain_utxo_token_data(u);
    REQUIRE(got != NULL);
    REQUIRE(kth_chain_token_data_get_amount(got) == 1234);

    kth_chain_utxo_destruct(u);
    kth_chain_token_data_destruct(td);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Copy + Equality
// ---------------------------------------------------------------------------

TEST_CASE("C-API Utxo - copy preserves fields and equals original",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 3u);
    kth_utxo_mut_t u = kth_chain_utxo_construct(op, 999u, NULL);
    kth_chain_utxo_set_height(u, 42u);

    kth_utxo_mut_t cp = kth_chain_utxo_copy(u);
    REQUIRE(cp != u);

    REQUIRE(kth_chain_utxo_equals(u, cp) != 0);
    REQUIRE(kth_chain_utxo_height(cp) == 42u);
    REQUIRE(kth_chain_utxo_amount(cp) == 999u);

    kth_chain_utxo_destruct(cp);
    kth_chain_utxo_destruct(u);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Utxo - mutating amount breaks equality",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 1u);
    kth_utxo_mut_t a = kth_chain_utxo_construct(op, 10u, NULL);
    kth_utxo_mut_t b = kth_chain_utxo_copy(a);

    REQUIRE(kth_chain_utxo_equals(a, b) != 0);
    kth_chain_utxo_set_amount(b, 11u);
    REQUIRE(kth_chain_utxo_equals(a, b) == 0);

    kth_chain_utxo_destruct(b);
    kth_chain_utxo_destruct(a);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Utxo - mutating height breaks equality",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 1u);
    kth_utxo_mut_t a = kth_chain_utxo_construct(op, 10u, NULL);
    kth_utxo_mut_t b = kth_chain_utxo_copy(a);

    REQUIRE(kth_chain_utxo_equals(a, b) != 0);
    kth_chain_utxo_set_height(b, 777u);
    REQUIRE(kth_chain_utxo_equals(a, b) == 0);

    kth_chain_utxo_destruct(b);
    kth_chain_utxo_destruct(a);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Utxo - mutating point breaks equality",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 1u);
    kth_utxo_mut_t a = kth_chain_utxo_construct(op, 10u, NULL);
    kth_utxo_mut_t b = kth_chain_utxo_copy(a);

    REQUIRE(kth_chain_utxo_equals(a, b) != 0);
    kth_output_point_mut_t op2 = kth_chain_output_point_construct_from_hash_index(&kTxid, 99u);
    kth_chain_utxo_set_point(b, op2);
    REQUIRE(kth_chain_utxo_equals(a, b) == 0);

    kth_chain_output_point_destruct(op2);
    kth_chain_utxo_destruct(b);
    kth_chain_utxo_destruct(a);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Utxo - mutating token_data breaks equality",
          "[C-API Utxo]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 1u);
    kth_utxo_mut_t a = kth_chain_utxo_construct(op, 10u, NULL);
    kth_utxo_mut_t b = kth_chain_utxo_copy(a);

    REQUIRE(kth_chain_utxo_equals(a, b) != 0);
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategory, 1u);
    kth_chain_utxo_set_token_data(b, td);
    REQUIRE(kth_chain_utxo_equals(a, b) == 0);

    kth_chain_token_data_destruct(td);
    kth_chain_utxo_destruct(b);
    kth_chain_utxo_destruct(a);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Utxo - setters round-trip through getters",
          "[C-API Utxo]") {
    kth_utxo_mut_t u = kth_chain_utxo_construct_default();

    kth_chain_utxo_set_height(u, 123u);
    kth_chain_utxo_set_amount(u, 456u);
    REQUIRE(kth_chain_utxo_height(u) == 123u);
    REQUIRE(kth_chain_utxo_amount(u) == 456u);

    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 9u);
    kth_chain_utxo_set_point(u, op);
    REQUIRE(kth_chain_output_point_index(kth_chain_utxo_point(u)) == 9u);

    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategory, 77u);
    kth_chain_utxo_set_token_data(u, td);
    REQUIRE(kth_chain_utxo_token_data(u) != NULL);
    REQUIRE(kth_chain_token_data_get_amount(kth_chain_utxo_token_data(u)) == 77);

    // Setting token_data to NULL clears the optional back to nullopt.
    kth_chain_utxo_set_token_data(u, NULL);
    REQUIRE(kth_chain_utxo_token_data(u) == NULL);

    kth_chain_token_data_destruct(td);
    kth_chain_output_point_destruct(op);
    kth_chain_utxo_destruct(u);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API Utxo - copy null aborts",
          "[C-API Utxo][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_utxo_copy(NULL));
}

TEST_CASE("C-API Utxo - equals null aborts",
          "[C-API Utxo][precondition]") {
    kth_utxo_mut_t u = kth_chain_utxo_construct_default();
    KTH_EXPECT_ABORT(kth_chain_utxo_equals(NULL, u));
    KTH_EXPECT_ABORT(kth_chain_utxo_equals(u, NULL));
    kth_chain_utxo_destruct(u);
}

TEST_CASE("C-API Utxo - getters null aborts",
          "[C-API Utxo][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_utxo_height(NULL));
    KTH_EXPECT_ABORT(kth_chain_utxo_amount(NULL));
    KTH_EXPECT_ABORT(kth_chain_utxo_point(NULL));
    KTH_EXPECT_ABORT(kth_chain_utxo_token_data(NULL));
}

TEST_CASE("C-API Utxo - construct null point aborts",
          "[C-API Utxo][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_utxo_construct(NULL, 0u, NULL));
}

TEST_CASE("C-API Utxo - set_point null aborts",
          "[C-API Utxo][precondition]") {
    kth_utxo_mut_t u = kth_chain_utxo_construct_default();
    KTH_EXPECT_ABORT(kth_chain_utxo_set_point(u, NULL));
    kth_chain_utxo_destruct(u);
}

TEST_CASE("C-API Utxo - setters null self aborts",
          "[C-API Utxo][precondition]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kTxid, 0u);
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategory, 1u);

    KTH_EXPECT_ABORT(kth_chain_utxo_set_height(NULL, 1u));
    KTH_EXPECT_ABORT(kth_chain_utxo_set_amount(NULL, 1u));
    KTH_EXPECT_ABORT(kth_chain_utxo_set_point(NULL, op));
    KTH_EXPECT_ABORT(kth_chain_utxo_set_token_data(NULL, td));

    kth_chain_token_data_destruct(td);
    kth_chain_output_point_destruct(op);
}
