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
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint32_t const kSequence = 0xfffffffeu;

// 20-byte payload that the C++ tests use as a script body. With wire=false
// the script parser just reads the raw operations from the buffer.
static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_hash_t const kPrevHash = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_script_mut_t make_script(void) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &script);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(script != NULL);
    return script;
}

static kth_output_point_mut_t make_outpoint(void) {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(kPrevHash, 7);
    REQUIRE(op != NULL);
    return op;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - default construct is invalid", "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    REQUIRE(kth_chain_input_is_valid(in) == 0);
    kth_chain_input_destruct(in);
}

TEST_CASE("C-API Input - field constructor preserves fields",
          "[C-API Input]") {
    kth_output_point_mut_t op = make_outpoint();
    kth_script_mut_t script = make_script();
    kth_input_mut_t in = kth_chain_input_construct(op, script, kSequence);
    REQUIRE(kth_chain_input_is_valid(in) != 0);
    REQUIRE(kth_chain_input_sequence(in) == kSequence);
    kth_chain_input_destruct(in);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Input - destruct null is safe", "[C-API Input]") {
    kth_chain_input_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - from_data insufficient bytes fails",
          "[C-API Input]") {
    uint8_t data[5];
    memset(data, 0, sizeof(data));
    kth_input_mut_t in = NULL;
    kth_error_code_t ec = kth_chain_input_construct_from_data(
        data, sizeof(data), 1, &in);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(in == NULL);
}

TEST_CASE("C-API Input - to_data / from_data roundtrip", "[C-API Input]") {
    kth_output_point_mut_t op = make_outpoint();
    kth_script_mut_t script = make_script();
    kth_input_mut_t expected = kth_chain_input_construct(op, script, kSequence);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_input_to_data(expected, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size > 0);

    kth_input_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_input_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_input_is_valid(parsed) != 0);
    REQUIRE(kth_chain_input_sequence(parsed) == kSequence);
    REQUIRE(kth_chain_input_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_input_destruct(parsed);
    kth_chain_input_destruct(expected);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - sequence setter roundtrip", "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    REQUIRE(kth_chain_input_sequence(in) != kSequence);
    kth_chain_input_set_sequence(in, kSequence);
    REQUIRE(kth_chain_input_sequence(in) == kSequence);
    kth_chain_input_destruct(in);
}

TEST_CASE("C-API Input - script setter roundtrip", "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    kth_script_mut_t script = make_script();
    kth_chain_input_set_script(in, script);
    // Read the borrowed view back and assert it equals what we wrote.
    // A no-op setter would leave the empty default script in place and
    // fail this comparison.
    kth_script_const_t view = kth_chain_input_script(in);
    REQUIRE(view != NULL);
    REQUIRE(kth_chain_script_equals(view, script) != 0);
    kth_chain_input_destruct(in);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Input - previous_output setter roundtrip", "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    kth_output_point_mut_t op = make_outpoint();
    kth_chain_input_set_previous_output(in, op);
    // Read the borrowed view back and assert it equals what we wrote.
    // The default outpoint has a zero hash and index 0, so a no-op
    // setter would not match the kPrevHash / index 7 fixture.
    kth_output_point_const_t view = kth_chain_input_previous_output(in);
    REQUIRE(view != NULL);
    REQUIRE(kth_chain_output_point_equals(view, op) != 0);
    kth_chain_input_destruct(in);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Predicates / computations
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - is_final on max sequence is true", "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    kth_chain_input_set_sequence(in, 0xffffffffu);
    REQUIRE(kth_chain_input_is_final(in) != 0);
    kth_chain_input_destruct(in);
}

TEST_CASE("C-API Input - is_final on non-max sequence is false",
          "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    kth_chain_input_set_sequence(in, 0u);
    REQUIRE(kth_chain_input_is_final(in) == 0);
    kth_chain_input_destruct(in);
}

TEST_CASE("C-API Input - signature_operations on default is zero",
          "[C-API Input]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    REQUIRE(kth_chain_input_signature_operations(in, 0, 0) == 0);
    kth_chain_input_destruct(in);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - copy preserves fields", "[C-API Input]") {
    kth_output_point_mut_t op = make_outpoint();
    kth_script_mut_t script = make_script();
    kth_input_mut_t original = kth_chain_input_construct(op, script, kSequence);
    kth_input_mut_t copy = kth_chain_input_copy(original);

    REQUIRE(kth_chain_input_is_valid(copy) != 0);
    REQUIRE(kth_chain_input_equals(original, copy) != 0);
    REQUIRE(kth_chain_input_sequence(copy) == kSequence);

    kth_chain_input_destruct(copy);
    kth_chain_input_destruct(original);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
}

TEST_CASE("C-API Input - equals different inputs", "[C-API Input]") {
    kth_output_point_mut_t op = make_outpoint();
    kth_script_mut_t script = make_script();
    kth_input_mut_t a = kth_chain_input_construct(op, script, kSequence);
    kth_input_mut_t b = kth_chain_input_construct(op, script, kSequence);
    kth_input_mut_t c = kth_chain_input_construct_default();

    REQUIRE(kth_chain_input_equals(a, b) != 0);
    REQUIRE(kth_chain_input_equals(a, c) == 0);

    kth_chain_input_destruct(a);
    kth_chain_input_destruct(b);
    kth_chain_input_destruct(c);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Input - construct_from_data null data with non-zero size aborts",
          "[C-API Input][precondition]") {
    KTH_EXPECT_ABORT({
        kth_input_mut_t in = NULL;
        kth_chain_input_construct_from_data(NULL, 1, 1, &in);
    });
}

TEST_CASE("C-API Input - construct_from_data null out aborts",
          "[C-API Input][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_input_construct_from_data(data, 10, 1, NULL));
}

TEST_CASE("C-API Input - to_data null out_size aborts",
          "[C-API Input][precondition]") {
    kth_input_mut_t in = kth_chain_input_construct_default();
    KTH_EXPECT_ABORT(kth_chain_input_to_data(in, 1, NULL));
    kth_chain_input_destruct(in);
}

TEST_CASE("C-API Input - copy null self aborts",
          "[C-API Input][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_input_copy(NULL));
}

TEST_CASE("C-API Input - construct null previous_output aborts",
          "[C-API Input][precondition]") {
    kth_script_mut_t script = make_script();
    KTH_EXPECT_ABORT(kth_chain_input_construct(NULL, script, kSequence));
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Input - construct null script aborts",
          "[C-API Input][precondition]") {
    kth_output_point_mut_t op = make_outpoint();
    KTH_EXPECT_ABORT(kth_chain_input_construct(op, NULL, kSequence));
    kth_chain_output_point_destruct(op);
}
