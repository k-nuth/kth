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

#include <kth/capi/chain/double_spend_proof.h>
#include <kth/capi/chain/double_spend_proof_spender.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static kth_hash_t const kHashA = {{
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
}};

static kth_hash_t const kHashB = {{
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef
}};

static kth_hash_t const kHashC = {{
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe
}};

// A minimal spender wire payload: 4+4+4 little-endian fields followed by
// three 32-byte hashes, with empty trailing push_data. Total = 108 bytes.
// Values: version=1, out_sequence=2, locktime=3, prev_outs_hash=kHashA,
// sequence_hash=kHashB, outputs_hash=kHashC, push_data=(empty).
static uint8_t const kSpenderWire[108] = {
    // version = 1
    0x01, 0x00, 0x00, 0x00,
    // out_sequence = 2
    0x02, 0x00, 0x00, 0x00,
    // locktime = 3
    0x03, 0x00, 0x00, 0x00,
    // prev_outs_hash (kHashA)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    // sequence_hash (kHashB)
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef,
    // outputs_hash (kHashC)
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe,
    0xca, 0xfe, 0xba, 0xbe, 0xca, 0xfe, 0xba, 0xbe
};

// ===========================================================================
// DoubleSpendProofSpender
// ===========================================================================

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - destruct null is safe",
          "[C-API DspSpender]") {
    kth_chain_double_spend_proof_spender_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / construct
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - from_data wire layout populates all fields",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(sp != NULL);

    REQUIRE(kth_chain_double_spend_proof_spender_is_valid(sp) != 0);
    REQUIRE(kth_chain_double_spend_proof_spender_version(sp) == 1u);
    REQUIRE(kth_chain_double_spend_proof_spender_out_sequence(sp) == 2u);
    REQUIRE(kth_chain_double_spend_proof_spender_locktime(sp) == 3u);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_prev_outs_hash(sp), kHashA) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_sequence_hash(sp), kHashB) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_outputs_hash(sp), kHashC) != 0);

    kth_size_t push_size = 0;
    uint8_t* push = kth_chain_double_spend_proof_spender_push_data(sp, &push_size);
    REQUIRE(push_size == 0u);
    kth_core_destruct_array(push);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - from_data insufficient bytes fails",
          "[C-API DspSpender]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    kth_double_spend_proof_spender_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        data, sizeof(data), 0u, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - serialized_size is 108 for empty push_data",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_serialized_size(sp) == 108u);
    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - serialized_size grows with push_data",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);

    uint8_t const push[5] = {1, 2, 3, 4, 5};
    kth_chain_double_spend_proof_spender_set_push_data(sp, push, sizeof(push));
    REQUIRE(kth_chain_double_spend_proof_spender_serialized_size(sp) == 113u);

    kth_size_t out_size = 0;
    uint8_t* got = kth_chain_double_spend_proof_spender_push_data(sp, &out_size);
    REQUIRE(out_size == sizeof(push));
    REQUIRE(memcmp(got, push, sizeof(push)) == 0);
    kth_core_destruct_array(got);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

// ---------------------------------------------------------------------------
// Setters / getters round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - scalar setters round-trip",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);

    kth_chain_double_spend_proof_spender_set_version(sp, 0xdeadbeefu);
    kth_chain_double_spend_proof_spender_set_out_sequence(sp, 0xcafebabu);
    kth_chain_double_spend_proof_spender_set_locktime(sp, 0x12345678u);

    REQUIRE(kth_chain_double_spend_proof_spender_version(sp) == 0xdeadbeefu);
    REQUIRE(kth_chain_double_spend_proof_spender_out_sequence(sp) == 0xcafebabu);
    REQUIRE(kth_chain_double_spend_proof_spender_locktime(sp) == 0x12345678u);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - hash setters round-trip",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);

    kth_chain_double_spend_proof_spender_set_prev_outs_hash(sp, &kHashB);
    kth_chain_double_spend_proof_spender_set_sequence_hash(sp, &kHashC);
    kth_chain_double_spend_proof_spender_set_outputs_hash(sp, &kHashA);

    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_prev_outs_hash(sp), kHashB) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_sequence_hash(sp), kHashC) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_outputs_hash(sp), kHashA) != 0);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - unsafe hash setters round-trip",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);

    kth_chain_double_spend_proof_spender_set_prev_outs_hash_unsafe(sp, kHashC.hash);
    kth_chain_double_spend_proof_spender_set_sequence_hash_unsafe(sp, kHashA.hash);
    kth_chain_double_spend_proof_spender_set_outputs_hash_unsafe(sp, kHashB.hash);

    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_prev_outs_hash(sp), kHashC) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_sequence_hash(sp), kHashA) != 0);
    REQUIRE(kth_hash_equal(kth_chain_double_spend_proof_spender_outputs_hash(sp), kHashB) != 0);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - set_push_data with NULL and zero n clears it",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);

    uint8_t const seed[3] = {9, 9, 9};
    kth_chain_double_spend_proof_spender_set_push_data(sp, seed, sizeof(seed));

    kth_chain_double_spend_proof_spender_set_push_data(sp, NULL, 0);
    kth_size_t out_size = 99u;
    uint8_t* got = kth_chain_double_spend_proof_spender_push_data(sp, &out_size);
    REQUIRE(out_size == 0u);
    kth_core_destruct_array(got);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - copy preserves equality",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t original = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &original);
    REQUIRE(ec == kth_ec_success);

    kth_double_spend_proof_spender_mut_t copy = kth_chain_double_spend_proof_spender_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(original, copy) != 0);

    kth_chain_double_spend_proof_spender_set_version(copy, 99u);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(original, copy) == 0);

    kth_chain_double_spend_proof_spender_destruct(copy);
    kth_chain_double_spend_proof_spender_destruct(original);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - reset zeroes all fields",
          "[C-API DspSpender]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_is_valid(sp) != 0);

    kth_chain_double_spend_proof_spender_reset(sp);

    REQUIRE(kth_chain_double_spend_proof_spender_is_valid(sp) == 0);
    REQUIRE(kth_chain_double_spend_proof_spender_version(sp) == 0u);
    REQUIRE(kth_chain_double_spend_proof_spender_out_sequence(sp) == 0u);
    REQUIRE(kth_chain_double_spend_proof_spender_locktime(sp) == 0u);
    REQUIRE(kth_hash_is_null(kth_chain_double_spend_proof_spender_prev_outs_hash(sp)) != 0);
    REQUIRE(kth_hash_is_null(kth_chain_double_spend_proof_spender_sequence_hash(sp)) != 0);
    REQUIRE(kth_hash_is_null(kth_chain_double_spend_proof_spender_outputs_hash(sp)) != 0);

    kth_chain_double_spend_proof_spender_destruct(sp);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API DspSpender - from_data null out aborts",
          "[C-API DspSpender][precondition]") {
    KTH_EXPECT_ABORT(
        kth_chain_double_spend_proof_spender_construct_from_data(kSpenderWire, sizeof(kSpenderWire), 0u, NULL));
}

TEST_CASE("C-API DspSpender - copy null self aborts",
          "[C-API DspSpender][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_copy(NULL));
}

TEST_CASE("C-API DspSpender - getter null aborts",
          "[C-API DspSpender][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_version(NULL));
}

TEST_CASE("C-API DspSpender - set_prev_outs_hash_unsafe null aborts",
          "[C-API DspSpender][precondition]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_set_prev_outs_hash_unsafe(sp, NULL));
    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - set_prev_outs_hash null aborts",
          "[C-API DspSpender][precondition]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_set_prev_outs_hash(sp, NULL));
    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - set_sequence_hash null aborts",
          "[C-API DspSpender][precondition]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_set_sequence_hash(sp, NULL));
    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API DspSpender - set_outputs_hash null aborts",
          "[C-API DspSpender][precondition]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    kth_error_code_t ec = kth_chain_double_spend_proof_spender_construct_from_data(
        kSpenderWire, sizeof(kSpenderWire), 0u, &sp);
    REQUIRE(ec == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_spender_set_outputs_hash(sp, NULL));
    kth_chain_double_spend_proof_spender_destruct(sp);
}

// ===========================================================================
// DoubleSpendProof
// ===========================================================================

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - default construct produces invalid proof",
          "[C-API Dsp]") {
    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct_default();
    REQUIRE(dsp != NULL);
    // A default-constructed DSP has a default out_point and zero-filled
    // spenders — therefore is_valid() == false.
    REQUIRE(kth_chain_double_spend_proof_is_valid(dsp) == 0);
    kth_chain_double_spend_proof_destruct(dsp);
}

TEST_CASE("C-API Dsp - destruct null is safe", "[C-API Dsp]") {
    kth_chain_double_spend_proof_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Construction from components
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - construct with valid components is valid",
          "[C-API Dsp]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashA, 7u);
    kth_double_spend_proof_spender_mut_t s1 = NULL;
    kth_double_spend_proof_spender_mut_t s2 = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s1) == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s2) == kth_ec_success);

    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct(op, s1, s2);
    REQUIRE(dsp != NULL);
    REQUIRE(kth_chain_double_spend_proof_is_valid(dsp) != 0);

    // out_point was copied by value; fields round-trip.
    kth_output_point_const_t op_view = kth_chain_double_spend_proof_out_point(dsp);
    REQUIRE(kth_chain_output_point_index(op_view) == 7u);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(op_view), kHashA) != 0);

    // spender1 / spender2 round-trip.
    kth_double_spend_proof_spender_const_t sp1_view = kth_chain_double_spend_proof_spender1(dsp);
    kth_double_spend_proof_spender_const_t sp2_view = kth_chain_double_spend_proof_spender2(dsp);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(sp1_view, s1) != 0);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(sp2_view, s2) != 0);

    kth_chain_double_spend_proof_destruct(dsp);
    kth_chain_double_spend_proof_spender_destruct(s2);
    kth_chain_double_spend_proof_spender_destruct(s1);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - serialized_size matches to_data length",
          "[C-API Dsp]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashA, 0u);
    kth_double_spend_proof_spender_mut_t s1 = NULL;
    kth_double_spend_proof_spender_mut_t s2 = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s1) == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s2) == kth_ec_success);

    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct(op, s1, s2);
    REQUIRE(dsp != NULL);

    kth_size_t const expected_size = kth_chain_double_spend_proof_serialized_size(dsp, 0u);
    // outpoint (36) + spender1 (108) + spender2 (108) = 252
    REQUIRE(expected_size == 252u);

    kth_size_t got_size = 0;
    uint8_t* raw = kth_chain_double_spend_proof_to_data(dsp, 0u, &got_size);
    REQUIRE(raw != NULL);
    REQUIRE(got_size == expected_size);

    kth_core_destruct_array(raw);
    kth_chain_double_spend_proof_destruct(dsp);
    kth_chain_double_spend_proof_spender_destruct(s2);
    kth_chain_double_spend_proof_spender_destruct(s1);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - setters replace fields",
          "[C-API Dsp]") {
    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct_default();
    REQUIRE(dsp != NULL);

    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashB, 42u);
    kth_chain_double_spend_proof_set_out_point(dsp, op);
    kth_output_point_const_t op_view = kth_chain_double_spend_proof_out_point(dsp);
    REQUIRE(kth_chain_output_point_index(op_view) == 42u);
    REQUIRE(kth_hash_equal(kth_chain_output_point_hash(op_view), kHashB) != 0);

    kth_double_spend_proof_spender_mut_t sp = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &sp) == kth_ec_success);

    kth_chain_double_spend_proof_set_spender1(dsp, sp);
    kth_chain_double_spend_proof_set_spender2(dsp, sp);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(kth_chain_double_spend_proof_spender1(dsp), sp) != 0);
    REQUIRE(kth_chain_double_spend_proof_spender_equals(kth_chain_double_spend_proof_spender2(dsp), sp) != 0);
    REQUIRE(kth_chain_double_spend_proof_is_valid(dsp) != 0);

    kth_chain_double_spend_proof_spender_destruct(sp);
    kth_chain_output_point_destruct(op);
    kth_chain_double_spend_proof_destruct(dsp);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - copy preserves equality and diverges after mutation",
          "[C-API Dsp]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashA, 7u);
    kth_double_spend_proof_spender_mut_t s1 = NULL;
    kth_double_spend_proof_spender_mut_t s2 = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s1) == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s2) == kth_ec_success);

    kth_double_spend_proof_mut_t original = kth_chain_double_spend_proof_construct(op, s1, s2);
    REQUIRE(original != NULL);
    kth_double_spend_proof_mut_t copy = kth_chain_double_spend_proof_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_double_spend_proof_equals(original, copy) != 0);

    // Mutate the copy — the two must diverge.
    kth_output_point_mut_t other_op = kth_chain_output_point_construct_from_hash_index(&kHashB, 99u);
    kth_chain_double_spend_proof_set_out_point(copy, other_op);
    REQUIRE(kth_chain_double_spend_proof_equals(original, copy) == 0);

    kth_chain_output_point_destruct(other_op);
    kth_chain_double_spend_proof_destruct(copy);
    kth_chain_double_spend_proof_destruct(original);
    kth_chain_double_spend_proof_spender_destruct(s2);
    kth_chain_double_spend_proof_spender_destruct(s1);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - hash is deterministic and mutation changes it",
          "[C-API Dsp]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashA, 0u);
    kth_double_spend_proof_spender_mut_t s1 = NULL;
    kth_double_spend_proof_spender_mut_t s2 = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s1) == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s2) == kth_ec_success);

    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct(op, s1, s2);
    REQUIRE(dsp != NULL);

    kth_hash_t h1 = kth_chain_double_spend_proof_hash(dsp);
    kth_hash_t h2 = kth_chain_double_spend_proof_hash(dsp);
    REQUIRE(kth_hash_equal(h1, h2) != 0);
    REQUIRE(kth_hash_is_null(h1) == 0);

    // Mutate and verify the hash changes.
    kth_double_spend_proof_spender_mut_t s_alt = kth_chain_double_spend_proof_spender_copy(s1);
    kth_chain_double_spend_proof_spender_set_version(s_alt, 99u);
    kth_chain_double_spend_proof_set_spender1(dsp, s_alt);
    kth_hash_t h3 = kth_chain_double_spend_proof_hash(dsp);
    REQUIRE(kth_hash_equal(h1, h3) == 0);

    kth_chain_double_spend_proof_spender_destruct(s_alt);
    kth_chain_double_spend_proof_destruct(dsp);
    kth_chain_double_spend_proof_spender_destruct(s2);
    kth_chain_double_spend_proof_spender_destruct(s1);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - reset drops to invalid state",
          "[C-API Dsp]") {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kHashA, 7u);
    kth_double_spend_proof_spender_mut_t s1 = NULL;
    kth_double_spend_proof_spender_mut_t s2 = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s1) == kth_ec_success);
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &s2) == kth_ec_success);

    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct(op, s1, s2);
    REQUIRE(dsp != NULL);
    REQUIRE(kth_chain_double_spend_proof_is_valid(dsp) != 0);

    kth_chain_double_spend_proof_reset(dsp);
    REQUIRE(kth_chain_double_spend_proof_is_valid(dsp) == 0);

    kth_chain_double_spend_proof_destruct(dsp);
    kth_chain_double_spend_proof_spender_destruct(s2);
    kth_chain_double_spend_proof_spender_destruct(s1);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API Dsp - copy null self aborts",
          "[C-API Dsp][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_copy(NULL));
}

TEST_CASE("C-API Dsp - equals null self aborts",
          "[C-API Dsp][precondition]") {
    kth_double_spend_proof_mut_t other = kth_chain_double_spend_proof_construct_default();
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_equals(NULL, other));
    kth_chain_double_spend_proof_destruct(other);
}

TEST_CASE("C-API Dsp - out_point null aborts",
          "[C-API Dsp][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_out_point(NULL));
}

TEST_CASE("C-API Dsp - construct null out_point aborts",
          "[C-API Dsp][precondition]") {
    kth_double_spend_proof_spender_mut_t sp = NULL;
    REQUIRE(kth_chain_double_spend_proof_spender_construct_from_data(
                kSpenderWire, sizeof(kSpenderWire), 0u, &sp) == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_construct(NULL, sp, sp));
    kth_chain_double_spend_proof_spender_destruct(sp);
}

TEST_CASE("C-API Dsp - construct_from_data null out aborts",
          "[C-API Dsp][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_construct_from_data(NULL, 0, 0u, NULL));
}

TEST_CASE("C-API Dsp - to_data null out_size aborts",
          "[C-API Dsp][precondition]") {
    kth_double_spend_proof_mut_t dsp = kth_chain_double_spend_proof_construct_default();
    KTH_EXPECT_ABORT(kth_chain_double_spend_proof_to_data(dsp, 0u, NULL));
    kth_chain_double_spend_proof_destruct(dsp);
}
