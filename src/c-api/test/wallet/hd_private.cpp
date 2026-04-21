// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/wallet/hd_private.h>
#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// BIP32 test vector 1 — master private key (from "000102030405060708090a0b0c0d0e0f" seed).
static char const* const kXprv =
    "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi";

// Matching xpub for the same seed.
static char const* const kXpub =
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";

// BIP32 test vector 1 seed (hex: 000102030405060708090a0b0c0d0e0f).
static uint8_t const kSeed[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPrivate - default construct is invalid", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_default();
    REQUIRE(kth_wallet_hd_private_valid(key) == 0);
    kth_wallet_hd_private_destruct(key);
}

TEST_CASE("C-API HdPrivate - construct from seed is valid", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_from_seed_prefixes(
        kSeed, sizeof(kSeed), 0x0488ADE40488B21Eull);  // mainnet prefixes
    REQUIRE(key != NULL);
    REQUIRE(kth_wallet_hd_private_valid(key) != 0);
    kth_wallet_hd_private_destruct(key);
}

TEST_CASE("C-API HdPrivate - construct from encoded is valid", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(key != NULL);
    REQUIRE(kth_wallet_hd_private_valid(key) != 0);
    kth_wallet_hd_private_destruct(key);
}

TEST_CASE("C-API HdPrivate - construct from invalid encoded returns null", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_from_encoded("not-a-valid-xprv");
    REQUIRE(key == NULL);
}

TEST_CASE("C-API HdPrivate - destruct null is safe", "[C-API HdPrivate]") {
    kth_wallet_hd_private_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPrivate - encoded round-trips", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(key != NULL);

    char* encoded = kth_wallet_hd_private_encoded(key);
    REQUIRE(encoded != NULL);
    REQUIRE(strcmp(encoded, kXprv) == 0);

    kth_core_destruct_string(encoded);
    kth_wallet_hd_private_destruct(key);
}

TEST_CASE("C-API HdPrivate - lineage returns valid data", "[C-API HdPrivate]") {
    kth_hd_private_mut_t key = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(key != NULL);

    kth_hd_lineage_t lineage = kth_wallet_hd_private_lineage(key);
    REQUIRE(lineage.depth == 0);
    REQUIRE(lineage.parent_fingerprint == 0);
    REQUIRE(lineage.child_number == 0);

    kth_wallet_hd_private_destruct(key);
}

TEST_CASE("C-API HdPrivate - to_hd_key round-trips", "[C-API HdPrivate]") {
    kth_hd_private_mut_t original = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(original != NULL);

    kth_hd_key_t hd_key = kth_wallet_hd_private_to_hd_key(original);
    kth_hd_private_mut_t reconstructed = kth_wallet_hd_private_construct_from_private_key(&hd_key);
    REQUIRE(reconstructed != NULL);
    REQUIRE(kth_wallet_hd_private_equals(original, reconstructed) != 0);

    kth_wallet_hd_private_destruct(reconstructed);
    kth_wallet_hd_private_destruct(original);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPrivate - copy preserves equality", "[C-API HdPrivate]") {
    kth_hd_private_mut_t original = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(original != NULL);

    kth_hd_private_mut_t copy = kth_wallet_hd_private_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(kth_wallet_hd_private_equals(original, copy) != 0);

    kth_wallet_hd_private_destruct(copy);
    kth_wallet_hd_private_destruct(original);
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPrivate - to_public produces matching xpub", "[C-API HdPrivate]") {
    kth_hd_private_mut_t priv = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(priv != NULL);

    kth_hd_public_mut_t pub = kth_wallet_hd_private_to_public(priv);
    REQUIRE(pub != NULL);
    REQUIRE(kth_wallet_hd_public_valid(pub) != 0);

    char* pub_encoded = kth_wallet_hd_public_encoded(pub);
    REQUIRE(strcmp(pub_encoded, kXpub) == 0);

    kth_core_destruct_string(pub_encoded);
    kth_wallet_hd_public_destruct(pub);
    kth_wallet_hd_private_destruct(priv);
}

TEST_CASE("C-API HdPrivate - derive_private produces BIP32 vector 1 child m/0h",
          "[C-API HdPrivate]") {
    kth_hd_private_mut_t parent = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(parent != NULL);

    // BIP32 vector 1: m/0' (hardened child 0)
    kth_hd_private_mut_t child = kth_wallet_hd_private_derive_private(parent, 0x80000000u);
    REQUIRE(child != NULL);
    REQUIRE(kth_wallet_hd_private_valid(child) != 0);

    kth_hd_lineage_t lineage = kth_wallet_hd_private_lineage(child);
    REQUIRE(lineage.depth == 1);

    // Verify against known BIP32 vector 1 child m/0'
    char* child_encoded = kth_wallet_hd_private_encoded(child);
    REQUIRE(strcmp(child_encoded, "xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ngLNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7") == 0);

    kth_core_destruct_string(child_encoded);
    kth_wallet_hd_private_destruct(child);
    kth_wallet_hd_private_destruct(parent);
}

TEST_CASE("C-API HdPrivate - derive_public produces valid child", "[C-API HdPrivate]") {
    kth_hd_private_mut_t parent = kth_wallet_hd_private_construct_from_encoded(kXprv);
    REQUIRE(parent != NULL);

    kth_hd_public_mut_t child = kth_wallet_hd_private_derive_public(parent, 0);
    REQUIRE(child != NULL);
    REQUIRE(kth_wallet_hd_public_valid(child) != 0);

    kth_wallet_hd_public_destruct(child);
    kth_wallet_hd_private_destruct(parent);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPrivate - construct_from_encoded null aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_construct_from_encoded(NULL));
}

TEST_CASE("C-API HdPrivate - copy null aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_copy(NULL));
}

TEST_CASE("C-API HdPrivate - construct_from_seed null data with non-zero size aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_construct_from_seed_prefixes(NULL, 16, 0));
}

TEST_CASE("C-API HdPrivate - construct_from_private_key null aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_construct_from_private_key(NULL));
}

TEST_CASE("C-API HdPrivate - construct_from_private_key_prefixes null aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_construct_from_private_key_prefixes(NULL, 0));
}

TEST_CASE("C-API HdPrivate - construct_from_private_key_prefix null aborts",
          "[C-API HdPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_private_construct_from_private_key_prefix(NULL, 0));
}
