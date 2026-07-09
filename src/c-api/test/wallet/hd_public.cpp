// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/error.h>
#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// BIP32 test vector 1 — master public key (from "000102030405060708090a0b0c0d0e0f" seed).
static char const* const kXpub =
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";

// ---------------------------------------------------------------------------
// Constructors / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - parse_from produces a handle", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &key) == kth_ec_success);
    REQUIRE(key != NULL);
    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - parse_from invalid encoded returns error", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from("not-a-valid-xpub", &key) != kth_ec_success);
    REQUIRE(key == NULL);
}

TEST_CASE("C-API HdPublic - destruct null is safe", "[C-API HdPublic]") {
    kth_wallet_hd_public_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - to_string round-trips", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &key) == kth_ec_success);
    REQUIRE(key != NULL);

    char* encoded = kth_wallet_hd_public_to_string(key);
    REQUIRE(encoded != NULL);
    REQUIRE(strcmp(encoded, kXpub) == 0);

    kth_core_destruct_string(encoded);
    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - lineage returns valid data", "[C-API HdPublic]") {
    kth_hd_public_mut_t key = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &key) == kth_ec_success);
    REQUIRE(key != NULL);

    kth_hd_lineage_t lineage = kth_wallet_hd_public_lineage(key);
    REQUIRE(lineage.depth == 0);
    REQUIRE(lineage.parent_fingerprint == 0);
    REQUIRE(lineage.child_number == 0);

    kth_wallet_hd_public_destruct(key);
}

TEST_CASE("C-API HdPublic - to_hd_key round-trips through from_hd_key",
          "[C-API HdPublic]") {
    kth_hd_public_mut_t original = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &original) == kth_ec_success);
    REQUIRE(original != NULL);

    kth_hd_key_t hd_key = kth_wallet_hd_public_to_hd_key(original);
    kth_hd_public_mut_t reconstructed = NULL;
    REQUIRE(kth_wallet_hd_public_from_hd_key(&hd_key, &reconstructed) == kth_ec_success);
    REQUIRE(reconstructed != NULL);
    REQUIRE(kth_wallet_hd_public_equals(original, reconstructed) != 0);

    kth_wallet_hd_public_destruct(reconstructed);
    kth_wallet_hd_public_destruct(original);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - copy preserves equality", "[C-API HdPublic]") {
    kth_hd_public_mut_t original = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &original) == kth_ec_success);
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

TEST_CASE("C-API HdPublic - derive_public produces a child", "[C-API HdPublic]") {
    kth_hd_public_mut_t parent = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &parent) == kth_ec_success);
    REQUIRE(parent != NULL);

    kth_hd_public_mut_t child = NULL;
    REQUIRE(kth_wallet_hd_public_derive_public(parent, 0, &child) == kth_ec_success);
    REQUIRE(child != NULL);
    REQUIRE(kth_wallet_hd_public_equals(parent, child) == 0);

    kth_hd_lineage_t lineage = kth_wallet_hd_public_lineage(child);
    REQUIRE(lineage.depth == 1);

    kth_wallet_hd_public_destruct(child);
    kth_wallet_hd_public_destruct(parent);
}

TEST_CASE("C-API HdPublic - derive_public hardened index reports failure",
          "[C-API HdPublic]") {
    kth_hd_public_mut_t parent = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &parent) == kth_ec_success);
    REQUIRE(parent != NULL);

    // Public derivation cannot produce hardened children.
    kth_hd_public_mut_t child = NULL;
    REQUIRE(kth_wallet_hd_public_derive_public(parent, 0x80000000u, &child) != kth_ec_success);
    REQUIRE(child == NULL);

    kth_wallet_hd_public_destruct(parent);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API HdPublic - parse_from null aborts",
          "[C-API HdPublic][precondition]") {
    kth_hd_public_mut_t out = NULL;
    KTH_EXPECT_ABORT(kth_wallet_hd_public_parse_from(NULL, &out));
}

TEST_CASE("C-API HdPublic - copy null aborts",
          "[C-API HdPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_hd_public_copy(NULL));
}

TEST_CASE("C-API HdPublic - equals null aborts",
          "[C-API HdPublic][precondition]") {
    kth_hd_public_mut_t other = NULL;
    REQUIRE(kth_wallet_hd_public_parse_from(kXpub, &other) == kth_ec_success);
    KTH_EXPECT_ABORT(kth_wallet_hd_public_equals(NULL, other));
    kth_wallet_hd_public_destruct(other);
}
