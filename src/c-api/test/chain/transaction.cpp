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

#include <kth/capi/chain/input.h>
#include <kth/capi/chain/input_list.h>
#include <kth/capi/chain/output.h>
#include <kth/capi/chain/output_list.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint32_t const kVersion  = 2u;
static uint32_t const kLocktime = 0u;

// Coinbase-like pre-image for the input's previous_output (any 32 bytes —
// these tests don't validate against a real prev tx).
static kth_hash_t const kPrevHash = {{
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf
}};

// 20-byte payload used as a script body. With wire=false the parser just
// reads raw operations from the buffer.
static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_script_mut_t make_script(void) {
    kth_script_mut_t s = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &s);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(s != NULL);
    return s;
}

// Build a single-input/single-output input_list and output_list pair.
// The caller takes ownership and must free with the matching destruct.
static kth_input_list_mut_t make_inputs(void) {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    REQUIRE(list != NULL);

    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kPrevHash, 0);
    REQUIRE(op != NULL);
    kth_script_mut_t script = make_script();
    kth_input_mut_t in = kth_chain_input_construct(op, script, 0xffffffffu);
    REQUIRE(in != NULL);

    kth_chain_input_list_push_back(list, in);

    kth_chain_input_destruct(in);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
    return list;
}

static kth_output_list_mut_t make_outputs(void) {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    REQUIRE(list != NULL);

    kth_script_mut_t script = make_script();
    kth_output_mut_t out = kth_chain_output_construct(50000ull, script, NULL);
    REQUIRE(out != NULL);

    kth_chain_output_list_push_back(list, out);

    kth_chain_output_destruct(out);
    kth_chain_script_destruct(script);
    return list;
}

// Build a complete one-in/one-out transaction. Caller frees with destruct.
static kth_transaction_mut_t make_tx(void) {
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    kth_transaction_mut_t tx = kth_chain_transaction_construct_from_version_locktime_inputs_outputs(
        kVersion, kLocktime, ins, outs);
    REQUIRE(tx != NULL);
    kth_chain_input_list_destruct(ins);
    kth_chain_output_list_destruct(outs);
    return tx;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - default construct is invalid",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    REQUIRE(kth_chain_transaction_is_valid(tx) == 0);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - field constructor preserves version & locktime",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = make_tx();
    REQUIRE(kth_chain_transaction_is_valid(tx) != 0);
    REQUIRE(kth_chain_transaction_version(tx) == kVersion);
    REQUIRE(kth_chain_transaction_locktime(tx) == kLocktime);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - destruct null is safe",
          "[C-API Transaction]") {
    kth_chain_transaction_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - from_data insufficient bytes fails",
          "[C-API Transaction]") {
    uint8_t data[5];
    memset(data, 0, sizeof(data));
    kth_transaction_mut_t tx = NULL;
    kth_error_code_t ec = kth_chain_transaction_construct_from_data(
        data, sizeof(data), 1, &tx);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(tx == NULL);
}

TEST_CASE("C-API Transaction - to_data / from_data roundtrip",
          "[C-API Transaction]") {
    kth_transaction_mut_t expected = make_tx();

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_transaction_to_data(expected, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size > 0);

    kth_transaction_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_transaction_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_transaction_is_valid(parsed) != 0);
    REQUIRE(kth_chain_transaction_version(parsed) == kVersion);
    REQUIRE(kth_chain_transaction_locktime(parsed) == kLocktime);
    REQUIRE(kth_chain_transaction_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_transaction_destruct(parsed);
    kth_chain_transaction_destruct(expected);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - version setter roundtrip",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    REQUIRE(kth_chain_transaction_version(tx) != kVersion);
    kth_chain_transaction_set_version(tx, kVersion);
    REQUIRE(kth_chain_transaction_version(tx) == kVersion);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - locktime setter roundtrip",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    kth_chain_transaction_set_locktime(tx, 12345u);
    REQUIRE(kth_chain_transaction_locktime(tx) == 12345u);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - inputs setter roundtrip",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    kth_input_list_mut_t ins = make_inputs();
    kth_chain_transaction_set_inputs(tx, ins);
    // Destroy the source list first to prove the setter deep-copied.
    // An aliasing setter would leave a dangling view after this point.
    kth_chain_input_list_destruct(ins);
    kth_input_list_const_t view = kth_chain_transaction_inputs(tx);
    REQUIRE(view != NULL);
    REQUIRE(kth_chain_input_list_count(view) == 1u);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - outputs setter roundtrip",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    kth_output_list_mut_t outs = make_outputs();
    kth_chain_transaction_set_outputs(tx, outs);
    kth_chain_output_list_destruct(outs);
    kth_output_list_const_t view = kth_chain_transaction_outputs(tx);
    REQUIRE(view != NULL);
    REQUIRE(kth_chain_output_list_count(view) == 1u);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Predicates / computations
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - default is_coinbase is false",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    REQUIRE(kth_chain_transaction_is_coinbase(tx) == 0);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - hash is stable across calls",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_hash_t a = kth_chain_transaction_hash(tx);
    kth_hash_t b = kth_chain_transaction_hash(tx);
    REQUIRE(memcmp(a.hash, b.hash, sizeof(a.hash)) == 0);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - serialized_size matches to_data length",
          "[C-API Transaction]") {
    kth_transaction_mut_t tx = make_tx();
    kth_size_t size = 0;
    uint8_t* raw = kth_chain_transaction_to_data(tx, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(kth_chain_transaction_serialized_size(tx, 1) == size);
    kth_core_destruct_array(raw);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - copy preserves fields",
          "[C-API Transaction]") {
    kth_transaction_mut_t original = make_tx();
    kth_transaction_mut_t copy = kth_chain_transaction_copy(original);

    REQUIRE(kth_chain_transaction_is_valid(copy) != 0);
    REQUIRE(kth_chain_transaction_equals(original, copy) != 0);
    REQUIRE(kth_chain_transaction_version(copy) == kVersion);
    REQUIRE(kth_chain_transaction_locktime(copy) == kLocktime);

    kth_chain_transaction_destruct(copy);
    kth_chain_transaction_destruct(original);
}

TEST_CASE("C-API Transaction - equals identical txs is true, different is false",
          "[C-API Transaction]") {
    // `a` and `b` come from the same fixture so they must compare equal.
    kth_transaction_mut_t a = make_tx();
    kth_transaction_mut_t b = make_tx();

    // `mutated` differs from `a` only in the locktime — this would catch
    // an `equals()` impl that ignores fields.
    kth_transaction_mut_t mutated = make_tx();
    kth_chain_transaction_set_locktime(mutated, kLocktime + 1u);
    kth_chain_transaction_recompute_hash(mutated);

    // `c` is a default (invalid) tx, structurally distinct from `a`.
    kth_transaction_mut_t c = kth_chain_transaction_construct_default();

    REQUIRE(kth_chain_transaction_equals(a, b) != 0);
    REQUIRE(kth_chain_transaction_equals(a, mutated) == 0);
    REQUIRE(kth_chain_transaction_equals(a, c) == 0);

    kth_chain_transaction_destruct(a);
    kth_chain_transaction_destruct(b);
    kth_chain_transaction_destruct(mutated);
    kth_chain_transaction_destruct(c);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction - construct_from_data null data with non-zero size aborts",
          "[C-API Transaction][precondition]") {
    KTH_EXPECT_ABORT({
        kth_transaction_mut_t out = NULL;
        kth_chain_transaction_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API Transaction - construct_from_data null out aborts",
          "[C-API Transaction][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_transaction_construct_from_data(data, 10, 1, NULL));
}

TEST_CASE("C-API Transaction - to_data null out_size aborts",
          "[C-API Transaction][precondition]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    KTH_EXPECT_ABORT(kth_chain_transaction_to_data(tx, 1, NULL));
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction - copy null self aborts",
          "[C-API Transaction][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_transaction_copy(NULL));
}

TEST_CASE("C-API Transaction - construct null inputs aborts",
          "[C-API Transaction][precondition]") {
    kth_output_list_mut_t outs = make_outputs();
    KTH_EXPECT_ABORT(
        kth_chain_transaction_construct_from_version_locktime_inputs_outputs(
            kVersion, kLocktime, NULL, outs));
    kth_chain_output_list_destruct(outs);
}

TEST_CASE("C-API Transaction - construct null outputs aborts",
          "[C-API Transaction][precondition]") {
    kth_input_list_mut_t ins = make_inputs();
    KTH_EXPECT_ABORT(
        kth_chain_transaction_construct_from_version_locktime_inputs_outputs(
            kVersion, kLocktime, ins, NULL));
    kth_chain_input_list_destruct(ins);
}
