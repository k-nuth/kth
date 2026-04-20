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

#include <kth/capi/chain/operation_list.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/chain/script_pattern.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/ec_public.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A trivial OP_RETURN script (one byte: 0x6a). Lets us round-trip a small
// known-valid payload through `from_data` / `to_data` without depending on
// the operation_list constructor (which we can not call from C-API yet).
//
// Wire layout: prefix byte (0x01 = length 1) followed by the OP_RETURN
// opcode (0x6a). With prefix=true, the parser reads the length first.
static uint8_t const kOpReturnPrefixed[2] = { 0x01, 0x6a };
static uint8_t const kOpReturnRaw[1]      = { 0x6a };

// 20-byte short hash (used for pay_to_script_hash patterns).
static kth_shorthash_t const kShortHash = {{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13
}};

// 32-byte hash (used for pay_to_script_hash_32 patterns).
static kth_hash_t const kHash32 = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - default construct is empty", "[C-API Script]") {
    kth_script_mut_t script = kth_chain_script_construct_default();
    REQUIRE(kth_chain_script_empty(script) != 0);
    REQUIRE(kth_chain_script_size(script) == 0u);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - destruct null is safe", "[C-API Script]") {
    kth_chain_script_destruct(NULL);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - from_data prefixed roundtrip", "[C-API Script]") {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(script != NULL);
    REQUIRE(kth_chain_script_is_valid(script) != 0);
    REQUIRE(kth_chain_script_size(script) == 1u);
    REQUIRE(kth_chain_script_empty(script) == 0);

    kth_size_t out_size = 0;
    uint8_t* raw = kth_chain_script_to_data(script, 1, &out_size);
    REQUIRE(raw != NULL);
    REQUIRE(out_size == sizeof(kOpReturnPrefixed));
    REQUIRE(memcmp(raw, kOpReturnPrefixed, sizeof(kOpReturnPrefixed)) == 0);

    kth_core_destruct_array(raw);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - serialized_size matches to_data length",
          "[C-API Script]") {
    kth_script_mut_t script = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script) == kth_ec_success);

    kth_size_t expected_with_prefix    = kth_chain_script_serialized_size(script, 1);
    kth_size_t expected_without_prefix = kth_chain_script_serialized_size(script, 0);
    REQUIRE(expected_with_prefix    == 2u);  // length byte + opcode
    REQUIRE(expected_without_prefix == 1u);  // opcode only

    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Container accessors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - non-empty script has size 1", "[C-API Script]") {
    kth_script_mut_t script = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script) == kth_ec_success);
    REQUIRE(kth_chain_script_size(script) == 1u);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - clear empties a non-empty script", "[C-API Script]") {
    kth_script_mut_t script = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script) == kth_ec_success);
    REQUIRE(kth_chain_script_empty(script) == 0);
    kth_chain_script_clear(script);
    REQUIRE(kth_chain_script_empty(script) != 0);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// from_string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - from_string empty mnemonic", "[C-API Script]") {
    kth_script_mut_t script = kth_chain_script_construct_default();
    REQUIRE(kth_chain_script_from_string(script, "") != 0);
    REQUIRE(kth_chain_script_empty(script) != 0);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - copy preserves equality", "[C-API Script]") {
    kth_script_mut_t original = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &original) == kth_ec_success);

    kth_script_mut_t copy = kth_chain_script_copy(original);
    REQUIRE(kth_chain_script_is_valid(copy) != 0);
    REQUIRE(kth_chain_script_equals(original, copy) != 0);
    REQUIRE(kth_chain_script_size(copy) == kth_chain_script_size(original));

    kth_chain_script_destruct(copy);
    kth_chain_script_destruct(original);
}

TEST_CASE("C-API Script - equals different scripts", "[C-API Script]") {
    kth_script_mut_t a = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &a) == kth_ec_success);
    kth_script_mut_t b = kth_chain_script_construct_default();

    REQUIRE(kth_chain_script_equals(a, b) == 0);

    kth_chain_script_destruct(a);
    kth_chain_script_destruct(b);
}

// ---------------------------------------------------------------------------
// Static pattern factories
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - to_pay_script_hash_pattern returns operation list",
          "[C-API Script]") {
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_script_hash_pattern(kShortHash);
    REQUIRE(ops != NULL);
    // We can't introspect operation_list element-by-element from the C-API
    // yet, but constructing a script from it should yield a valid script.
    kth_script_mut_t script = kth_chain_script_construct_from_operations(ops);
    REQUIRE(kth_chain_script_is_valid(script) != 0);
    REQUIRE(kth_chain_script_empty(script) == 0);
    kth_chain_script_destruct(script);
    kth_chain_operation_list_destruct(ops);
}

TEST_CASE("C-API Script - to_pay_script_hash_32_pattern returns operation list",
          "[C-API Script]") {
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_script_hash_32_pattern(kHash32);
    REQUIRE(ops != NULL);
    kth_script_mut_t script = kth_chain_script_construct_from_operations(ops);
    REQUIRE(kth_chain_script_is_valid(script) != 0);
    REQUIRE(kth_chain_script_empty(script) == 0);
    kth_chain_script_destruct(script);
    kth_chain_operation_list_destruct(ops);
}

// ---------------------------------------------------------------------------
// Empty-as-failure list factories: return NULL when the C++ layer
// signals an invalid input via an empty result vector.
// ---------------------------------------------------------------------------

// A valid 33-byte compressed secp256k1 public key (BIP32 test vector 1
// master public key), usable to exercise the success path of the
// pay_to_public_key factories.
static uint8_t const kValidCompressedPoint[33] = {
    0x03, 0x39, 0xa3, 0x60, 0x13, 0x30, 0x15, 0x97,
    0xda, 0xef, 0x41, 0xfb, 0xe5, 0x93, 0xa0, 0x2c,
    0xc5, 0x13, 0xd0, 0xb5, 0x55, 0x27, 0xec, 0x2d,
    0xf1, 0x05, 0x0e, 0x2e, 0x8f, 0xf4, 0x9c, 0x85,
    0xc2
};

TEST_CASE("C-API Script - to_null_data_pattern valid data returns list",
          "[C-API Script]") {
    uint8_t const data[4] = { 0x01, 0x02, 0x03, 0x04 };
    kth_operation_list_mut_t ops =
        kth_chain_script_to_null_data_pattern(data, sizeof(data));
    REQUIRE(ops != NULL);
    kth_chain_operation_list_destruct(ops);
}

TEST_CASE("C-API Script - to_null_data_pattern oversized data returns NULL",
          "[C-API Script]") {
    // The max_null_data_size limit is 80 bytes; one byte over triggers
    // the empty-list failure signal in the C++ layer.
    uint8_t data[81];
    memset(data, 0x42, sizeof(data));
    kth_operation_list_mut_t ops =
        kth_chain_script_to_null_data_pattern(data, sizeof(data));
    REQUIRE(ops == NULL);
}

TEST_CASE("C-API Script - to_pay_public_key_pattern valid point returns list",
          "[C-API Script]") {
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_pattern(
            kValidCompressedPoint, sizeof(kValidCompressedPoint));
    REQUIRE(ops != NULL);
    kth_chain_operation_list_destruct(ops);
}

TEST_CASE("C-API Script - to_pay_public_key_pattern malformed point returns NULL",
          "[C-API Script]") {
    // A 33-byte blob with an invalid prefix (0x00) fails the
    // is_public_key check, so the C++ factory yields an empty list.
    uint8_t malformed[33];
    memset(malformed, 0x00, sizeof(malformed));
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_pattern(malformed, sizeof(malformed));
    REQUIRE(ops == NULL);
}

TEST_CASE("C-API Script - to_pay_public_key_pattern wrong-size point returns NULL",
          "[C-API Script]") {
    // Neither 33 (compressed) nor 65 (uncompressed) bytes — fails the
    // size check inside is_public_key.
    uint8_t too_short[10];
    memset(too_short, 0x03, sizeof(too_short));
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_pattern(too_short, sizeof(too_short));
    REQUIRE(ops == NULL);
}

TEST_CASE("C-API Script - to_pay_public_key_hash_pattern_unlocking valid pubkey returns list",
          "[C-API Script]") {
    kth_ec_public_mut_t pub =
        kth_wallet_ec_public_construct_from_decoded(
            kValidCompressedPoint, sizeof(kValidCompressedPoint));
    REQUIRE(pub != NULL);

    // A 71-byte DER-like endorsement blob; content is not validated at
    // this layer, we just need something non-empty to push.
    uint8_t endorsement[71];
    memset(endorsement, 0x30, sizeof(endorsement));

    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_hash_pattern_unlocking(
            endorsement, sizeof(endorsement), pub);
    REQUIRE(ops != NULL);

    kth_chain_operation_list_destruct(ops);
    kth_wallet_ec_public_destruct(pub);
}

TEST_CASE("C-API Script - to_pay_public_key_hash_pattern_unlocking invalid pubkey returns NULL",
          "[C-API Script]") {
    // Default-constructed ec_public has valid_ == false, so its
    // to_data() returns an error and the C++ factory yields an empty
    // unlocking list that we turn into NULL.
    kth_ec_public_mut_t pub = kth_wallet_ec_public_construct_default();
    REQUIRE(pub != NULL);
    REQUIRE(kth_wallet_ec_public_valid(pub) == 0);

    uint8_t endorsement[71];
    memset(endorsement, 0x30, sizeof(endorsement));

    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_hash_pattern_unlocking(
            endorsement, sizeof(endorsement), pub);
    REQUIRE(ops == NULL);

    kth_wallet_ec_public_destruct(pub);
}

TEST_CASE("C-API Script - to_pay_public_key_hash_pattern always returns a list",
          "[C-API Script]") {
    // Control case: sibling pattern that is NOT opt-in to the
    // empty-as-failure convention. Any 20-byte hash produces a valid
    // 5-op list, so the factory never returns NULL.
    kth_operation_list_mut_t ops =
        kth_chain_script_to_pay_public_key_hash_pattern(kShortHash);
    REQUIRE(ops != NULL);
    kth_chain_operation_list_destruct(ops);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - is_enabled bit test", "[C-API Script]") {
    kth_size_t flags = 0x05;  // bits 0 and 2 set
    REQUIRE(kth_chain_script_is_enabled(flags, 0x01) != 0);
    REQUIRE(kth_chain_script_is_enabled(flags, 0x04) != 0);
    REQUIRE(kth_chain_script_is_enabled(flags, 0x02) == 0);
}

// ---------------------------------------------------------------------------
// Pattern detection
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - pattern of empty script is non standard",
          "[C-API Script]") {
    kth_script_mut_t script = kth_chain_script_construct_default();
    kth_script_pattern_t p = kth_chain_script_pattern(script);
    REQUIRE(p == kth_script_pattern_non_standard);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - bare OP_RETURN is non_standard",
          "[C-API Script]") {
    // A single OP_RETURN with no payload does not match the canonical
    // null_data pattern (which requires a data push). The classifier
    // should fall through to non_standard.
    kth_script_mut_t script = NULL;
    REQUIRE(kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script) == kth_ec_success);
    kth_script_pattern_t p = kth_chain_script_output_pattern_simple(script);
    REQUIRE(p == kth_script_pattern_non_standard);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - construct_from_data null data with non-zero size aborts",
          "[C-API Script][precondition]") {
    KTH_EXPECT_ABORT({
        kth_script_mut_t out = NULL;
        kth_chain_script_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API Script - construct_from_data NULL data with zero size succeeds",
          "[C-API Script]") {
    // Empty script is a valid input. (NULL, 0) should not abort and should
    // produce an empty, valid script.
    kth_script_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(NULL, 0, 0, &out);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(out != NULL);
    REQUIRE(kth_chain_script_empty(out) != 0);
    kth_chain_script_destruct(out);
}

TEST_CASE("C-API Script - construct_from_data null out aborts",
          "[C-API Script][precondition]") {
    uint8_t data[2] = { 0x00, 0x00 };
    KTH_EXPECT_ABORT(kth_chain_script_construct_from_data(data, 2, 1, NULL));
}

TEST_CASE("C-API Script - to_data null out_size aborts",
          "[C-API Script][precondition]") {
    kth_script_mut_t script = kth_chain_script_construct_default();
    KTH_EXPECT_ABORT(kth_chain_script_to_data(script, 1, NULL));
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - copy null self aborts",
          "[C-API Script][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_script_copy(NULL));
}

TEST_CASE("C-API Script - equals null self aborts",
          "[C-API Script][precondition]") {
    kth_script_mut_t other = kth_chain_script_construct_default();
    KTH_EXPECT_ABORT(kth_chain_script_equals(NULL, other));
    kth_chain_script_destruct(other);
}

// Safe `kth_chain_script_to_pay_script_hash_pattern` takes the short
// hash by value: passing NULL is a compile error. The runtime
// precondition still applies on the `_unsafe` companion.
TEST_CASE("C-API Script - to_pay_script_hash_pattern_unsafe null hash aborts",
          "[C-API Script][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_script_to_pay_script_hash_pattern_unsafe(NULL));
}

TEST_CASE("C-API Script - at() on empty script aborts",
          "[C-API Script][precondition]") {
    kth_script_mut_t script = kth_chain_script_construct_default();
    KTH_EXPECT_ABORT(kth_chain_script_at(script, 0));
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Script - at() out of bounds aborts",
          "[C-API Script][precondition]") {
    // Reuse the file-level OP_RETURN fixture: 1 operation at index 0.
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kOpReturnPrefixed, sizeof(kOpReturnPrefixed), 1, &script);
    REQUIRE(ec == kth_ec_success);
    KTH_EXPECT_ABORT(kth_chain_script_at(script, 1));
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Regression: null public_key with zero size must not UB
// ---------------------------------------------------------------------------

TEST_CASE("C-API Script - check_signature with null public_key and zero size",
          "[C-API Script][regression]") {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    kth_script_mut_t script = kth_chain_script_construct_default();
    kth_longhash_t sig;
    memset(sig.hash, 0, sizeof(sig.hash));
    kth_size_t out_size = 0;

    // public_key=NULL, public_key_n=0 is valid per the precondition.
    // Must not crash (data_chunk(nullptr, nullptr+0) is UB).
    kth_bool_t result = kth_chain_script_check_signature(
        sig, 0, NULL, 0, script, tx, 0, 0, 0, &out_size);
    (void)result;

    kth_chain_script_destruct(script);
    kth_chain_transaction_destruct(tx);
}
