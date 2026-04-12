// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// BIP32 test vector 1 — master public key (from "000102030405060708090a0b0c0d0e0f" seed).
static char const* const kXpub =
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - default construct is invalid", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = kth_wallet_hd_public_construct_default();
    REQUIRE(kth_wallet_hd_public_valid(key) == 0);
    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - construct from encoded is valid", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(key != NULL);
    REQUIRE(kth_wallet_hd_public_valid(key) != 0);
    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - construct from invalid encoded returns null", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = kth_wallet_hd_public_construct_from_encoded("not-a-valid-xpub");
    REQUIRE(key == NULL);
}

TEST_CASE("C-API HdPublic - destruct null is safe", "[C-API HdPublic]") {
    kth_wallet_hd_public_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - encoded round-trips", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(key != NULL);

    char* encoded = kth_wallet_hd_public_encoded(key);
    REQUIRE(encoded != NULL);
    REQUIRE(strcmp(encoded, kXpub) == 0);

    kth_core_destruct_string(encoded);
    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - lineage returns valid data", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(key != NULL);

    kth_hd_lineage_t lineage = kth_wallet_hd_public_lineage(key);
    REQUIRE(lineage.depth == 0);
    REQUIRE(lineage.parent_fingerprint == 0);
    REQUIRE(lineage.child_number == 0);

    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - to_hd_key round-trips through construct_from_public_key",
          "[C-API HdPublic]") {
    kth_hd_public_mut_t original = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(original != NULL);

    kth_hd_key_t hd_key = kth_wallet_hd_public_to_hd_key(original);
    kth_hd_public_mut_t reconstructed = kth_wallet_hd_public_construct_from_public_key(hd_key);
    REQUIRE(reconstructed != NULL);
    REQUIRE(kth_wallet_hd_public_equals(original, reconstructed) != 0);

    kth_wallet_hd_public_destruct(reconstructed);
    kth_wallet_hd_public_destruct(original);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - copy preserves equality", "[C-API HdPublic]") {
    kth_hd_public_mut_t original = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(original != NULL);

    kth_hd_public_mut_t copy = kth_wallet_hd_public_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_wallet_hd_public_equals(original, copy) != 0);

    kth_wallet_hd_public_destruct(copy);
    kth_wallet_hd_public_destruct(original);
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - derive_public produces valid child", "[C-API HdPublic]") {
    kth_hd_public_mut_t parent = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(parent != NULL);

    kth_hd_public_mut_t child = kth_wallet_hd_public_derive_public(parent, 0);
    REQUIRE(child != NULL);
    REQUIRE(kth_wallet_hd_public_valid(child) != 0);
    REQUIRE(kth_wallet_hd_public_equals(parent, child) == 0);

    kth_hd_lineage_t lineage = kth_wallet_hd_public_lineage(child);
    REQUIRE(lineage.depth == 1);

    kth_wallet_hd_public_destruct(child);
    kth_wallet_hd_public_destruct(parent);
}

TEST_CASE("C-API HdPublic - derive_public hardened index returns null",
          "[C-API HdPublic]") {
    kth_hd_public_mut_t parent = kth_wallet_hd_public_construct_from_encoded(kXpub);
    REQUIRE(parent != NULL);

    // Public derivation cannot produce hardened children.
    kth_hd_public_mut_t child = kth_wallet_hd_public_derive_public(parent, 0x80000000u);
    REQUIRE(child == NULL);

    kth_wallet_hd_public_destruct(parent);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - construct_from_encoded null aborts",
          "[C-API HdPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_public_construct_from_encoded(NULL));
}

TEST_CASE("C-API HdPublic - copy null aborts",
          "[C-API HdPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_public_copy(NULL));
}

TEST_CASE("C-API HdPublic - equals null aborts",
          "[C-API HdPublic][precondition]") {
    kth_hd_public_mut_t other = kth_wallet_hd_public_construct_from_encoded(kXpub);
    KTH_EXPECT_ABORT(kth_wallet_hd_public_equals(NULL, other));
    kth_wallet_hd_public_destruct(other);
}
