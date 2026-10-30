// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Contract tests for the Transaction closure (transaction, input, output,
// output_point, script and their lists) written as a plain C consumer would
// use them: ownership transfer, borrowed const views, out-params left
// untouched on error, null arguments, buffers and destructors.
//
// The same file is compiled against the manually maintained C-API and the
// regenerated one. Tests for functions that only
// exist in the regenerated C-API are fenced by KTH_CAPI_TEST_NO_CONSTRUCT_MOVE.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/endorsement_type.h>
#include <kth/capi/chain/script_flags.h>
#include <kth/capi/handles.h>
#include <kth/capi/domain/chain/input.h>
#include <kth/capi/domain/chain/input_list.h>
#include <kth/capi/domain/chain/output.h>
#include <kth/capi/domain/chain/output_list.h>
#include <kth/capi/domain/chain/output_point.h>
#include <kth/capi/domain/chain/script.h>
#include <kth/capi/domain/chain/transaction.h>
#include <kth/capi/domain/chain/transaction_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static kth_hash_t const kPrev = {{
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30
}};

static uint8_t const kBody[3] = { 0x51, 0x51, 0x93 };

#if defined(KTH_CURRENCY_BCH)
#define kForkIdFlags kth_script_flags_bch_sighash_forkid
#else
#define kForkIdFlags kth_script_flags_bip143_rule
#endif

static kth_script_mut_t closure_script(void) {
    kth_script_mut_t s = NULL;
    REQUIRE(kth_domain_chain_script_construct_from_data(
        kBody, sizeof(kBody), 0, &s) == kth_ec_success);
    REQUIRE(s != NULL);
    return s;
}

static kth_input_list_mut_t closure_inputs(void) {
    kth_input_list_mut_t list = kth_domain_chain_input_list_construct_default();
    kth_output_point_mut_t op = kth_domain_chain_output_point_construct_from_hash_index(&kPrev, 3);
    kth_script_mut_t sc = closure_script();
    kth_input_mut_t in = kth_domain_chain_input_construct(op, sc, 0xfffffffeu);
    kth_domain_chain_input_list_push_back(list, in);
    kth_domain_chain_input_destruct(in);
    kth_domain_chain_script_destruct(sc);
    kth_domain_chain_output_point_destruct(op);
    return list;
}

static kth_output_list_mut_t closure_outputs(void) {
    kth_output_list_mut_t list = kth_domain_chain_output_list_construct_default();
    kth_script_mut_t sc = closure_script();
    kth_output_mut_t out = kth_domain_chain_output_construct(12345ull, sc, NULL);
    kth_domain_chain_output_list_push_back(list, out);
    kth_domain_chain_output_destruct(out);
    kth_domain_chain_script_destruct(sc);
    return list;
}

static kth_transaction_mut_t closure_tx(void) {
    kth_input_list_mut_t ins = closure_inputs();
    kth_output_list_mut_t outs = closure_outputs();
    kth_transaction_mut_t tx = kth_domain_chain_transaction_construct(7u, 99u, ins, outs);
    REQUIRE(tx != NULL);
    kth_domain_chain_input_list_destruct(ins);
    kth_domain_chain_output_list_destruct(outs);
    return tx;
}

// ---------------------------------------------------------------------------
// Ownership: copies are independent; borrowed views follow the owner
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction closure - copy outlives the original",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_transaction_mut_t copy = kth_domain_chain_transaction_copy(tx);
    kth_hash_t h_before = kth_domain_chain_transaction_hash(tx);
    kth_domain_chain_transaction_destruct(tx);

    REQUIRE(kth_domain_chain_transaction_version(copy) == 7u);
    REQUIRE(kth_domain_chain_transaction_locktime(copy) == 99u);
    kth_hash_t h_after = kth_domain_chain_transaction_hash(copy);
    REQUIRE(memcmp(h_before.hash, h_after.hash, sizeof(h_before.hash)) == 0);
    kth_domain_chain_transaction_destruct(copy);
}

TEST_CASE("C-API Transaction closure - inputs and outputs are borrowed const views",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_input_list_const_t ins = kth_domain_chain_transaction_inputs(tx);
    kth_output_list_const_t outs = kth_domain_chain_transaction_outputs(tx);
    REQUIRE(kth_domain_chain_input_list_count(ins) == 1);
    REQUIRE(kth_domain_chain_output_list_count(outs) == 1);
    // Repeated getters borrow the same storage; they never allocate a copy.
    REQUIRE(kth_domain_chain_transaction_inputs(tx) == ins);
    REQUIRE(kth_domain_chain_transaction_outputs(tx) == outs);
    kth_output_const_t out0 = kth_domain_chain_output_list_nth(outs, 0);
    REQUIRE(kth_domain_chain_output_value(out0) == 12345ull);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction closure - constructor does not consume its list arguments",
          "[C-API Transaction closure]") {
    kth_input_list_mut_t ins = closure_inputs();
    kth_output_list_mut_t outs = closure_outputs();
    kth_transaction_mut_t tx = kth_domain_chain_transaction_construct(1u, 0u, ins, outs);
    REQUIRE(kth_domain_chain_input_list_count(ins) == 1);
    REQUIRE(kth_domain_chain_output_list_count(outs) == 1);
    kth_domain_chain_transaction_destruct(tx);
    kth_domain_chain_input_list_destruct(ins);
    kth_domain_chain_output_list_destruct(outs);
}

// ---------------------------------------------------------------------------
// Error handling and out-params
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction closure - from_data failure leaves the out slot null",
          "[C-API Transaction closure]") {
    uint8_t const truncated[3] = { 1, 0, 0 };
    kth_transaction_mut_t out = NULL;
    kth_error_code_t ec = kth_domain_chain_transaction_construct_from_data(
        truncated, sizeof(truncated), 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Transaction closure - from_data into a non-null slot aborts",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t out = kth_domain_chain_transaction_construct_default();
    uint8_t const data[1] = { 0 };
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_from_data(data, 1, 1, &out));
    kth_domain_chain_transaction_destruct(out);
}

TEST_CASE("C-API Transaction closure - from_data failure keeps the exact domain error code",
          "[C-API Transaction closure]") {
    uint8_t const truncated[3] = { 1, 0, 0 };
    kth_transaction_mut_t out = NULL;
    REQUIRE(kth_domain_chain_transaction_construct_from_data(
        truncated, sizeof(truncated), 1, &out) == kth_ec_read_past_end_of_buffer);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Transaction closure - boundary error codes are distinct from every domain code",
          "[C-API Transaction closure]") {
    REQUIRE(kth_ec_out_of_memory == 10000);
    REQUIRE(kth_ec_unhandled_exception == 10001);
    REQUIRE(kth_ec_out_of_memory > kth_ec_operand_size_mismatch);
    REQUIRE(kth_ec_unhandled_exception > kth_ec_operand_size_mismatch);
}

TEST_CASE("C-API Transaction closure - to_data buffer round-trips and is caller-owned",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_size_t size = 0;
    uint8_t* raw = kth_domain_chain_transaction_to_data(tx, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size == kth_domain_chain_transaction_serialized_size(tx, 1));

    kth_transaction_mut_t parsed = NULL;
    REQUIRE(kth_domain_chain_transaction_construct_from_data(raw, size, 1, &parsed) == kth_ec_success);
    REQUIRE(kth_domain_chain_transaction_equals(tx, parsed) != 0);
    kth_core_destruct_array(raw);
    kth_domain_chain_transaction_destruct(parsed);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction closure - create_endorsement failure leaves outputs untouched",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_script_mut_t prevout = closure_script();
    kth_hash_t const zero_secret = {{ 0 }};   // not a valid secp256k1 secret
    uint8_t* out = NULL;
    kth_size_t out_size = 0xdeadu;
    kth_error_code_t ec = kth_domain_chain_script_create_endorsement(
        &zero_secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
        kth_endorsement_type_ecdsa, &out, &out_size);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
    REQUIRE(out_size == 0xdeadu);
    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction closure - create_endorsement success publishes an owned buffer",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_script_mut_t prevout = closure_script();
    kth_hash_t secret = {{ 0 }};
    secret.hash[31] = 1;
    uint8_t* out = NULL;
    kth_size_t out_size = 0;
    kth_error_code_t ec = kth_domain_chain_script_create_endorsement(
        &secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
        kth_endorsement_type_ecdsa, &out, &out_size);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(out != NULL);
    REQUIRE(out_size > 0);
    kth_core_destruct_array(out);
    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API Transaction closure - create_endorsement into a non-null slot aborts",
          "[C-API Transaction closure]") {
    kth_transaction_mut_t tx = closure_tx();
    kth_script_mut_t prevout = closure_script();
    kth_hash_t secret = {{ 0 }};
    secret.hash[31] = 1;
    uint8_t stale = 0;
    uint8_t* out = &stale;
    kth_size_t out_size = 0;
    KTH_EXPECT_ABORT(kth_domain_chain_script_create_endorsement(
        &secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
        kth_endorsement_type_ecdsa, &out, &out_size));
    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Nulls
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction closure - null handles abort",
          "[C-API Transaction closure]") {
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_version(NULL));
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_inputs(NULL));
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_hash(NULL));
    KTH_EXPECT_ABORT(kth_domain_chain_input_list_count(NULL));
}

// ---------------------------------------------------------------------------
// Lists of transactions copy their elements
// ---------------------------------------------------------------------------

TEST_CASE("C-API Transaction closure - transaction list stores copies",
          "[C-API Transaction closure]") {
    kth_transaction_list_mut_t list = kth_domain_chain_transaction_list_construct_default();
    kth_transaction_mut_t tx = closure_tx();
    kth_domain_chain_transaction_list_push_back(list, tx);
    kth_domain_chain_transaction_destruct(tx);
    REQUIRE(kth_domain_chain_transaction_list_count(list) == 1);
    kth_transaction_const_t first = kth_domain_chain_transaction_list_nth(list, 0);
    REQUIRE(kth_domain_chain_transaction_version(first) == 7u);
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_list_nth(list, 1));
    kth_domain_chain_transaction_list_destruct(list);
}

// ---------------------------------------------------------------------------
// Consuming constructor (regenerated C-API only)
// ---------------------------------------------------------------------------

#ifndef KTH_CAPI_TEST_NO_CONSTRUCT_MOVE

TEST_CASE("C-API Transaction closure - construct_move consumes both lists",
          "[C-API Transaction closure]") {
    kth_input_list_mut_t ins = closure_inputs();
    kth_output_list_mut_t outs = closure_outputs();
    kth_transaction_mut_t moved = NULL;
    REQUIRE(kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &moved) == kth_ec_success);
    REQUIRE(ins == NULL);
    REQUIRE(outs == NULL);
    REQUIRE(moved != NULL);

    kth_transaction_mut_t reference = closure_tx();
    REQUIRE(kth_domain_chain_transaction_equals(moved, reference) != 0);
    kth_domain_chain_transaction_destruct(reference);
    kth_domain_chain_transaction_destruct(moved);
}

TEST_CASE("C-API Transaction closure - construct_move contract violations abort",
          "[C-API Transaction closure]") {
    kth_input_list_mut_t ins = closure_inputs();
    kth_output_list_mut_t outs = closure_outputs();
    kth_transaction_mut_t out = NULL;
    kth_input_list_mut_t null_ins = NULL;

    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_move(1u, 0u, NULL, &outs, &out));
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_move(1u, 0u, &null_ins, &outs, &out));
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_move(1u, 0u, &ins, NULL, &out));
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_move(1u, 0u, &ins, &outs, NULL));

    kth_transaction_mut_t occupied = kth_domain_chain_transaction_construct_default();
    KTH_EXPECT_ABORT(kth_domain_chain_transaction_construct_move(1u, 0u, &ins, &outs, &occupied));
    kth_domain_chain_transaction_destruct(occupied);

    // Aborting children never touched the parent's handles.
    REQUIRE(ins != NULL);
    REQUIRE(outs != NULL);
    kth_domain_chain_input_list_destruct(ins);
    kth_domain_chain_output_list_destruct(outs);
}

#endif // KTH_CAPI_TEST_NO_CONSTRUCT_MOVE
