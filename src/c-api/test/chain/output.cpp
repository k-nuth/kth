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

#include <kth/capi/chain/output.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint64_t const kValue = 523542ull;

// 20-byte payload that the C++ tests use as a script body. With wire=false
// the script parser just reads the raw operations from the buffer.
static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_script_mut_t make_script(void) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &script);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(script != NULL);
    return script;
}

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - default construct is invalid", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    REQUIRE(kth_chain_output_is_valid(op) == 0);
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - field constructor preserves value and script",
          "[C-API Output]") {
    kth_script_mut_t script = make_script();
    kth_output_mut_t op = kth_chain_output_construct(kValue, script, NULL);
    REQUIRE(kth_chain_output_is_valid(op) != 0);
    REQUIRE(kth_chain_output_value(op) == kValue);
    // No token data was provided → token_data() returns NULL.
    REQUIRE(kth_chain_output_token_data(op) == NULL);
    kth_chain_output_destruct(op);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Output - destruct null is safe", "[C-API Output]") {
    kth_chain_output_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - from_data insufficient bytes fails",
          "[C-API Output]") {
    uint8_t data[5];
    memset(data, 0, sizeof(data));
    kth_output_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_output_construct_from_data(
        data, sizeof(data), 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Output - to_data / from_data roundtrip", "[C-API Output]") {
    kth_script_mut_t script = make_script();
    kth_output_mut_t expected = kth_chain_output_construct(kValue, script, NULL);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_output_to_data(expected, 1, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size > 0);

    kth_output_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_output_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_output_is_valid(parsed) != 0);
    REQUIRE(kth_chain_output_value(parsed) == kValue);
    REQUIRE(kth_chain_output_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_output_destruct(parsed);
    kth_chain_output_destruct(expected);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - value setter roundtrip", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    REQUIRE(kth_chain_output_value(op) != kValue);
    kth_chain_output_set_value(op, kValue);
    REQUIRE(kth_chain_output_value(op) == kValue);
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - script setter roundtrip", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    kth_script_mut_t script = make_script();
    kth_chain_output_set_script(op, script);
    // Borrowed view back; just exercise the path.
    REQUIRE(kth_chain_output_script(op) != NULL);
    kth_chain_output_destruct(op);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Output - default token_data is NULL", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    REQUIRE(kth_chain_output_token_data(op) == NULL);
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - set_token_data NULL clears the optional",
          "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    // Already NULL by default; setting NULL must remain a no-op (no abort).
    kth_chain_output_set_token_data(op, NULL);
    REQUIRE(kth_chain_output_token_data(op) == NULL);
    kth_chain_output_destruct(op);
}

// ---------------------------------------------------------------------------
// Predicates / computations
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - is_dust below minimum is true", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    kth_chain_output_set_value(op, 100);
    REQUIRE(kth_chain_output_is_dust(op, 1000) != 0);
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - is_dust above minimum is false", "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    kth_chain_output_set_value(op, 100000);
    REQUIRE(kth_chain_output_is_dust(op, 1000) == 0);
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - signature_operations on default is zero",
          "[C-API Output]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    REQUIRE(kth_chain_output_signature_operations(op, 0) == 0);
    kth_chain_output_destruct(op);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - copy preserves fields", "[C-API Output]") {
    kth_script_mut_t script = make_script();
    kth_output_mut_t original = kth_chain_output_construct(kValue, script, NULL);
    kth_output_mut_t copy = kth_chain_output_copy(original);

    REQUIRE(kth_chain_output_is_valid(copy) != 0);
    REQUIRE(kth_chain_output_equals(original, copy) != 0);
    REQUIRE(kth_chain_output_value(copy) == kValue);

    kth_chain_output_destruct(copy);
    kth_chain_output_destruct(original);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Output - equals different values", "[C-API Output]") {
    kth_script_mut_t script = make_script();
    kth_output_mut_t a = kth_chain_output_construct(kValue, script, NULL);
    kth_output_mut_t b = kth_chain_output_construct(kValue, script, NULL);
    kth_output_mut_t c = kth_chain_output_construct_default();

    REQUIRE(kth_chain_output_equals(a, b) != 0);
    REQUIRE(kth_chain_output_equals(a, c) == 0);

    kth_chain_output_destruct(a);
    kth_chain_output_destruct(b);
    kth_chain_output_destruct(c);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Output - construct_from_data null data with non-zero size aborts",
          "[C-API Output][precondition]") {
    KTH_EXPECT_ABORT({
        kth_output_mut_t out = NULL;
        kth_chain_output_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API Output - construct_from_data null out aborts",
          "[C-API Output][precondition]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    KTH_EXPECT_ABORT(kth_chain_output_construct_from_data(data, 10, 1, NULL));
}

TEST_CASE("C-API Output - to_data null out_size aborts",
          "[C-API Output][precondition]") {
    kth_output_mut_t op = kth_chain_output_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_to_data(op, 1, NULL));
    kth_chain_output_destruct(op);
}

TEST_CASE("C-API Output - copy null self aborts",
          "[C-API Output][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_copy(NULL));
}

TEST_CASE("C-API Output - construct null script aborts",
          "[C-API Output][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_output_construct(kValue, NULL, NULL));
}
