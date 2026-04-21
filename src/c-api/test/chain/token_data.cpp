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

#include <kth/capi/chain/token_capability.h>
#include <kth/capi/chain/token_data.h>
#include <kth/capi/chain/token_kind.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Token category IDs are 32-byte hash digests.
static kth_hash_t const kCategoryA = {{
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
}};

static kth_hash_t const kCategoryB = {{
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb
}};

// A 5-byte sample commitment for the NFT tests.
static uint8_t const kCommitment[5] = {0x10, 0x20, 0x30, 0x40, 0x50};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - destruct null is safe", "[C-API TokenData]") {
    kth_chain_token_data_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Factories — fungible
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - make_fungible builds a valid handle",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 1234u);
    REQUIRE(td != NULL);
    REQUIRE(kth_chain_token_data_is_valid(td) != 0);
    REQUIRE(kth_chain_token_data_get_kind(td) == kth_token_kind_fungible_only);
    REQUIRE(kth_chain_token_data_is_fungible_only(td) != 0);
    REQUIRE(kth_chain_token_data_has_nft(td) == 0);
    REQUIRE(kth_chain_token_data_get_amount(td) == 1234);
    REQUIRE(kth_hash_equal(kth_chain_token_data_id(td), kCategoryA) != 0);

    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - make_fungible_unsafe builds the same handle",
          "[C-API TokenData]") {
    kth_token_data_mut_t safe = kth_chain_token_make_fungible(&kCategoryA, 7u);
    kth_token_data_mut_t unsafe = kth_chain_token_make_fungible_unsafe(kCategoryA.hash, 7u);

    REQUIRE(safe != NULL);
    REQUIRE(unsafe != NULL);
    REQUIRE(kth_chain_token_data_equals(safe, unsafe) != 0);

    kth_chain_token_data_destruct(unsafe);
    kth_chain_token_data_destruct(safe);
}

// ---------------------------------------------------------------------------
// Factories — non_fungible
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - make_non_fungible mutable carries commitment and capability",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_non_fungible(
        &kCategoryA, kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    REQUIRE(td != NULL);

    REQUIRE(kth_chain_token_data_is_valid(td) != 0);
    REQUIRE(kth_chain_token_data_get_kind(td) == kth_token_kind_non_fungible_only);
    REQUIRE(kth_chain_token_data_has_nft(td) != 0);
    REQUIRE(kth_chain_token_data_is_fungible_only(td) == 0);
    REQUIRE(kth_chain_token_data_is_mutable_nft(td) != 0);
    REQUIRE(kth_chain_token_data_is_immutable_nft(td) == 0);
    REQUIRE(kth_chain_token_data_is_minting_nft(td) == 0);

    REQUIRE(kth_chain_token_data_get_nft_capability(td) == kth_token_capability_mut);

    kth_size_t commitment_size = 0;
    uint8_t* commitment = kth_chain_token_data_get_nft_commitment(td, &commitment_size);
    REQUIRE(commitment_size == sizeof(kCommitment));
    REQUIRE(memcmp(commitment, kCommitment, sizeof(kCommitment)) == 0);
    kth_core_destruct_array(commitment);

    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - make_non_fungible immutable",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_non_fungible(
        &kCategoryA, kth_token_capability_none, kCommitment, sizeof(kCommitment));
    REQUIRE(td != NULL);
    REQUIRE(kth_chain_token_data_is_immutable_nft(td) != 0);
    REQUIRE(kth_chain_token_data_is_mutable_nft(td) == 0);
    REQUIRE(kth_chain_token_data_is_minting_nft(td) == 0);
    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - make_non_fungible minting",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_non_fungible(
        &kCategoryA, kth_token_capability_minting, kCommitment, sizeof(kCommitment));
    REQUIRE(td != NULL);
    REQUIRE(kth_chain_token_data_is_minting_nft(td) != 0);
    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - make_non_fungible with empty commitment",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_non_fungible(
        &kCategoryA, kth_token_capability_none, NULL, 0);
    REQUIRE(td != NULL);

    kth_size_t commitment_size = 99u;
    uint8_t* commitment = kth_chain_token_data_get_nft_commitment(td, &commitment_size);
    REQUIRE(commitment_size == 0u);
    // create_c_array returns a valid (possibly zero-sized) buffer when
    // the underlying data_chunk is empty — caller still releases it.
    kth_core_destruct_array(commitment);

    kth_chain_token_data_destruct(td);
}

// ---------------------------------------------------------------------------
// Factories — both
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - make_both carries amount, capability, commitment",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_both(
        &kCategoryA, 9999u, kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    REQUIRE(td != NULL);

    REQUIRE(kth_chain_token_data_is_valid(td) != 0);
    REQUIRE(kth_chain_token_data_get_kind(td) == kth_token_kind_both);
    REQUIRE(kth_chain_token_data_has_nft(td) != 0);
    REQUIRE(kth_chain_token_data_is_fungible_only(td) == 0);
    REQUIRE(kth_chain_token_data_is_mutable_nft(td) != 0);

    REQUIRE(kth_chain_token_data_get_amount(td) == 9999);
    REQUIRE(kth_chain_token_data_get_nft_capability(td) == kth_token_capability_mut);

    kth_size_t commitment_size = 0;
    uint8_t* commitment = kth_chain_token_data_get_nft_commitment(td, &commitment_size);
    REQUIRE(commitment_size == sizeof(kCommitment));
    REQUIRE(memcmp(commitment, kCommitment, sizeof(kCommitment)) == 0);
    kth_core_destruct_array(commitment);

    kth_chain_token_data_destruct(td);
}

// ---------------------------------------------------------------------------
// Factories reject spec-invalid input by returning NULL
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - make_fungible rejects amount 0",
          "[C-API TokenData]") {
    // Per the CashTokens spec a valid fungible amount is strictly
    // positive. The binding layer's `check_valid` gate sits on top of
    // `operator bool` (which delegates to is_valid), so 0 yields NULL.
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 0u);
    REQUIRE(td == NULL);
}

TEST_CASE("C-API TokenData - make_fungible rejects amounts above INT64_MAX",
          "[C-API TokenData]") {
    // The CHIP caps amounts at INT64_MAX. UINT64_MAX (all ones) is
    // provably outside the spec range and must be rejected.
    kth_token_data_mut_t td = kth_chain_token_make_fungible(
        &kCategoryA, (uint64_t)0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(td == NULL);
}

TEST_CASE("C-API TokenData - make_both rejects amount 0",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_both(
        &kCategoryA, 0u, kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    REQUIRE(td == NULL);
}

// ---------------------------------------------------------------------------
// get_amount sentinel
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - get_amount returns 0 for pure NFT",
          "[C-API TokenData]") {
    // Per the CashTokens spec a valid fungible amount is strictly
    // positive, so `0` unambiguously signals "no fungible payload".
    // The consensus code in transaction_basis depends on this — see
    // the comment on `get_amount` in token_data.hpp.
    kth_token_data_mut_t td = kth_chain_token_make_non_fungible(
        &kCategoryA, kth_token_capability_none, kCommitment, sizeof(kCommitment));
    REQUIRE(td != NULL);
    REQUIRE(kth_chain_token_data_get_amount(td) == 0);
    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - get_nft_capability is `none` for pure fungible",
          "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 1u);
    REQUIRE(td != NULL);
    REQUIRE(kth_chain_token_data_get_nft_capability(td) == kth_token_capability_none);
    kth_chain_token_data_destruct(td);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - copy preserves equality", "[C-API TokenData]") {
    kth_token_data_mut_t original = kth_chain_token_make_both(
        &kCategoryA, 42u, kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    REQUIRE(original != NULL);

    kth_token_data_mut_t copy = kth_chain_token_data_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_chain_token_data_equals(original, copy) != 0);

    kth_chain_token_data_destruct(copy);
    kth_chain_token_data_destruct(original);
}

TEST_CASE("C-API TokenData - different categories compare unequal",
          "[C-API TokenData]") {
    kth_token_data_mut_t a = kth_chain_token_make_fungible(&kCategoryA, 1u);
    kth_token_data_mut_t b = kth_chain_token_make_fungible(&kCategoryB, 1u);

    REQUIRE(kth_chain_token_data_equals(a, b) == 0);

    kth_chain_token_data_destruct(b);
    kth_chain_token_data_destruct(a);
}

TEST_CASE("C-API TokenData - different amounts compare unequal",
          "[C-API TokenData]") {
    kth_token_data_mut_t a = kth_chain_token_make_fungible(&kCategoryA, 1u);
    kth_token_data_mut_t b = kth_chain_token_make_fungible(&kCategoryA, 2u);

    REQUIRE(kth_chain_token_data_equals(a, b) == 0);

    kth_chain_token_data_destruct(b);
    kth_chain_token_data_destruct(a);
}

// ---------------------------------------------------------------------------
// Serialization roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - to_data / from_data roundtrip (fungible)",
          "[C-API TokenData]") {
    kth_token_data_mut_t original = kth_chain_token_make_fungible(&kCategoryA, 7777u);
    REQUIRE(original != NULL);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_token_data_to_data(original, &size);
    REQUIRE(raw != NULL);
    REQUIRE(size == kth_chain_token_data_serialized_size(original));

    kth_token_data_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_token_construct_from_data(raw, size, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_token_data_equals(original, parsed) != 0);

    kth_chain_token_data_destruct(parsed);
    kth_core_destruct_array(raw);
    kth_chain_token_data_destruct(original);
}

TEST_CASE("C-API TokenData - to_data / from_data roundtrip (both)",
          "[C-API TokenData]") {
    kth_token_data_mut_t original = kth_chain_token_make_both(
        &kCategoryA, 12345u, kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    REQUIRE(original != NULL);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_token_data_to_data(original, &size);
    REQUIRE(raw != NULL);

    kth_token_data_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_token_construct_from_data(raw, size, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_token_data_equals(original, parsed) != 0);

    kth_chain_token_data_destruct(parsed);
    kth_core_destruct_array(raw);
    kth_chain_token_data_destruct(original);
}

TEST_CASE("C-API TokenData - from_data with truncated input fails",
          "[C-API TokenData]") {
    uint8_t const truncated[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    kth_token_data_mut_t parsed = NULL;
    kth_error_code_t ec =
        kth_chain_token_construct_from_data(truncated, sizeof(truncated), &parsed);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(parsed == NULL);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - set_id replaces category", "[C-API TokenData]") {
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 1u);
    REQUIRE(td != NULL);

    kth_chain_token_data_set_id(td, &kCategoryB);
    REQUIRE(kth_hash_equal(kth_chain_token_data_id(td), kCategoryB) != 0);

    kth_chain_token_data_destruct(td);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API TokenData - copy null aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_data_copy(NULL));
}

TEST_CASE("C-API TokenData - get_kind null aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_data_get_kind(NULL));
}

TEST_CASE("C-API TokenData - construct_from_data null out aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(
        kth_chain_token_construct_from_data(NULL, 0, NULL));
}

TEST_CASE("C-API TokenData - construct_from_data non-null *out aborts",
          "[C-API TokenData][precondition]") {
    kth_token_data_mut_t already = kth_chain_token_make_fungible(&kCategoryA, 1u);
    KTH_EXPECT_ABORT(
        kth_chain_token_construct_from_data(NULL, 0, &already));
    kth_chain_token_data_destruct(already);
}

TEST_CASE("C-API TokenData - to_data null out_size aborts",
          "[C-API TokenData][precondition]") {
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 1u);
    KTH_EXPECT_ABORT(kth_chain_token_data_to_data(td, NULL));
    kth_chain_token_data_destruct(td);
}

TEST_CASE("C-API TokenData - make_fungible_unsafe null id aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_make_fungible_unsafe(NULL, 1u));
}

TEST_CASE("C-API TokenData - make_fungible null id aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_make_fungible(NULL, 1u));
}

TEST_CASE("C-API TokenData - make_non_fungible null id aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_make_non_fungible(
        NULL, kth_token_capability_none, kCommitment, sizeof(kCommitment)));
}

TEST_CASE("C-API TokenData - make_both null id aborts",
          "[C-API TokenData][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_token_make_both(
        NULL, 1u, kth_token_capability_mut, kCommitment, sizeof(kCommitment)));
}

TEST_CASE("C-API TokenData - set_id null value aborts",
          "[C-API TokenData][precondition]") {
    kth_token_data_mut_t td = kth_chain_token_make_fungible(&kCategoryA, 1u);
    REQUIRE(td != NULL);
    KTH_EXPECT_ABORT(kth_chain_token_data_set_id(td, NULL));
    kth_chain_token_data_destruct(td);
}
