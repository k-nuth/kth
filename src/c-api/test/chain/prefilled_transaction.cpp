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

#include <kth/capi/chain/prefilled_transaction.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint32_t const kProtoVersion = 70015u;

// A minimal serialized transaction: version=1, zero inputs, zero outputs,
// locktime=0. Not semantically valid, but it round-trips through to_data /
// from_data, which is all prefilled_transaction cares about.
static uint8_t const kMinimalTx[10] = {
    0x01, 0x00, 0x00, 0x00,  // version = 1 (LE)
    0x00,                    // input count = 0
    0x00,                    // output count = 0
    0x00, 0x00, 0x00, 0x00   // locktime = 0
};

static kth_transaction_mut_t make_tx(void) {
    kth_transaction_mut_t tx = NULL;
    kth_error_code_t ec = kth_chain_transaction_construct_from_data(
        kMinimalTx, sizeof(kMinimalTx), 1, &tx);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(tx != NULL);
    return tx;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - default construct is invalid",
          "[C-API PrefilledTransaction]") {
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct_default();
    REQUIRE(kth_chain_prefilled_transaction_is_valid(pt) == 0);
    kth_chain_prefilled_transaction_destruct(pt);
}

TEST_CASE("C-API PrefilledTransaction - field constructor preserves index",
          "[C-API PrefilledTransaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct(7u, tx);
    REQUIRE(pt != NULL);
    REQUIRE(kth_chain_prefilled_transaction_index(pt) == 7u);
    kth_chain_prefilled_transaction_destruct(pt);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API PrefilledTransaction - destruct null is safe",
          "[C-API PrefilledTransaction]") {
    kth_chain_prefilled_transaction_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - to_data / from_data round-trip",
          "[C-API PrefilledTransaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t original =
        kth_chain_prefilled_transaction_construct(3u, tx);

    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_prefilled_transaction_to_data(
        original, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size > 0u);

    kth_prefilled_transaction_mut_t decoded = NULL;
    kth_error_code_t ec = kth_chain_prefilled_transaction_construct_from_data(
        raw, out_size, kProtoVersion, &decoded);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(decoded != NULL);
    REQUIRE(kth_chain_prefilled_transaction_equals(original, decoded) != 0);

    kth_core_destruct_array(raw);
    kth_chain_prefilled_transaction_destruct(decoded);
    kth_chain_prefilled_transaction_destruct(original);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API PrefilledTransaction - serialized_size matches to_data length",
          "[C-API PrefilledTransaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct(0u, tx);

    kth_size_t expected =
        kth_chain_prefilled_transaction_serialized_size(pt, kProtoVersion);
    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_prefilled_transaction_to_data(
        pt, kProtoVersion, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == expected);

    kth_core_destruct_array(raw);
    kth_chain_prefilled_transaction_destruct(pt);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - copy preserves equality",
          "[C-API PrefilledTransaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t original =
        kth_chain_prefilled_transaction_construct(5u, tx);

    kth_prefilled_transaction_mut_t copy =
        kth_chain_prefilled_transaction_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_prefilled_transaction_equals(original, copy) != 0);

    kth_chain_prefilled_transaction_destruct(copy);
    kth_chain_prefilled_transaction_destruct(original);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - set_index round-trips",
          "[C-API PrefilledTransaction]") {
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct_default();
    kth_chain_prefilled_transaction_set_index(pt, 42u);
    REQUIRE(kth_chain_prefilled_transaction_index(pt) == 42u);
    kth_chain_prefilled_transaction_destruct(pt);
}

TEST_CASE("C-API PrefilledTransaction - transaction getter returns borrowed view",
          "[C-API PrefilledTransaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct(1u, tx);

    kth_transaction_const_t inner = kth_chain_prefilled_transaction_transaction(pt);
    REQUIRE(inner != NULL);
    // Do NOT destruct `inner` — it is borrowed.

    kth_chain_prefilled_transaction_destruct(pt);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - reset invalidates the object",
          "[C-API PrefilledTransaction]") {
    // C++ `reset()` sets index_ to a sentinel value (max_uint32) and
    // clears the transaction, so is_valid() flips to false. The numeric
    // value of index after reset is an implementation detail, but the
    // validity flip is the observable contract.
    kth_transaction_mut_t tx = make_tx();
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct(9u, tx);
    kth_chain_prefilled_transaction_reset(pt);
    REQUIRE(kth_chain_prefilled_transaction_is_valid(pt) == 0);
    kth_chain_prefilled_transaction_destruct(pt);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API PrefilledTransaction - construct null tx aborts",
          "[C-API PrefilledTransaction][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_construct(0u, NULL));
}

TEST_CASE("C-API PrefilledTransaction - construct_from_data null data with non-zero size aborts",
          "[C-API PrefilledTransaction][precondition]") {
    KTH_EXPECT_ABORT({
        kth_prefilled_transaction_mut_t out = NULL;
        kth_chain_prefilled_transaction_construct_from_data(
            NULL, 1, kProtoVersion, &out);
    });
}

TEST_CASE("C-API PrefilledTransaction - construct_from_data null out aborts",
          "[C-API PrefilledTransaction][precondition]") {
    uint8_t data[2] = { 0x00, 0x00 };
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_construct_from_data(
        data, 2, kProtoVersion, NULL));
}

TEST_CASE("C-API PrefilledTransaction - to_data null out_size aborts",
          "[C-API PrefilledTransaction][precondition]") {
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct_default();
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_to_data(
        pt, kProtoVersion, NULL));
    kth_chain_prefilled_transaction_destruct(pt);
}

TEST_CASE("C-API PrefilledTransaction - copy null aborts",
          "[C-API PrefilledTransaction][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_copy(NULL));
}

TEST_CASE("C-API PrefilledTransaction - equals null aborts",
          "[C-API PrefilledTransaction][precondition]") {
    kth_prefilled_transaction_mut_t other =
        kth_chain_prefilled_transaction_construct_default();
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_equals(NULL, other));
    kth_chain_prefilled_transaction_destruct(other);
}

TEST_CASE("C-API PrefilledTransaction - transaction getter null aborts",
          "[C-API PrefilledTransaction][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_transaction(NULL));
}

TEST_CASE("C-API PrefilledTransaction - set_transaction null value aborts",
          "[C-API PrefilledTransaction][precondition]") {
    kth_prefilled_transaction_mut_t pt =
        kth_chain_prefilled_transaction_construct_default();
    KTH_EXPECT_ABORT(kth_chain_prefilled_transaction_set_transaction(pt, NULL));
    kth_chain_prefilled_transaction_destruct(pt);
}
