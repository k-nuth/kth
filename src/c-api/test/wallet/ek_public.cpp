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

#include <kth/capi/primitives.h>
#include <kth/capi/wallet/ek_public.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — BIP38 vectors lifted from the domain test suite. The
// "cfrm" prefix is the base58-checked signature for encrypted public
// keys (BIP38 "confirmation code" format, deprecated but still
// round-trippable).
// ---------------------------------------------------------------------------

static char const* const kEncrypted =
    "cfrm38V8aXBn7JWA1ESmFMUn6erxeBGZGAxJPY4e36S9QWkzZKtaVqLNMgnifETYw7BPwWC9aPD";

static char const* const kEncryptedOther =
    "cfrm38V8G4qq2ywYEFfWLD5Cc6msj9UwsG2Mj4Z6QdGJAFQpdatZLavkgRd1i4iBMdRngDqDs51";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_public - default construct is invalid",
          "[C-API WalletEkPublic][lifecycle]") {
    kth_ek_public_mut_t a = kth_wallet_ek_public_construct_default();
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_public_valid(a) == 0);
    kth_wallet_ek_public_destruct(a);
}

TEST_CASE("C-API wallet::ek_public - destruct(NULL) is a no-op",
          "[C-API WalletEkPublic][lifecycle]") {
    kth_wallet_ek_public_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Encoded string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_public - encoded round-trips through construct_from_encoded",
          "[C-API WalletEkPublic][encode]") {
    kth_ek_public_mut_t a = kth_wallet_ek_public_construct_from_encoded(kEncrypted);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_public_valid(a) != 0);

    char* back = kth_wallet_ek_public_encoded(a);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_public_destruct(a);
}

TEST_CASE("C-API wallet::ek_public - invalid encoded string fails to construct",
          "[C-API WalletEkPublic][encode]") {
    kth_ek_public_mut_t a = kth_wallet_ek_public_construct_from_encoded("not-a-key");
    REQUIRE(a == NULL);
}

// ---------------------------------------------------------------------------
// Value (raw byte payload) round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_public - public_key byte payload round-trip",
          "[C-API WalletEkPublic][encode]") {
    // Exercises both directions of the `kth_encrypted_public_t` ↔
    // `encrypted_public` value-struct bridge (55-byte payload).
    kth_ek_public_mut_t orig = kth_wallet_ek_public_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_public_t bytes = kth_wallet_ek_public_public_key(orig);

    kth_ek_public_mut_t rebuilt = kth_wallet_ek_public_construct_from_value(&bytes);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_public_valid(rebuilt) != 0);

    char* back = kth_wallet_ek_public_encoded(rebuilt);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_public_destruct(rebuilt);
    kth_wallet_ek_public_destruct(orig);
}

TEST_CASE("C-API wallet::ek_public - construct_from_value_unsafe matches safe variant",
          "[C-API WalletEkPublic][encode]") {
    kth_ek_public_mut_t orig = kth_wallet_ek_public_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_public_t bytes = kth_wallet_ek_public_public_key(orig);

    kth_ek_public_mut_t rebuilt =
        kth_wallet_ek_public_construct_from_value_unsafe(bytes.data);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_public_equals(orig, rebuilt) != 0);

    kth_wallet_ek_public_destruct(rebuilt);
    kth_wallet_ek_public_destruct(orig);
}

// ---------------------------------------------------------------------------
// Copy / equality / ordering
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_public - copy preserves value equality",
          "[C-API WalletEkPublic][value]") {
    kth_ek_public_mut_t a = kth_wallet_ek_public_construct_from_encoded(kEncrypted);
    kth_ek_public_mut_t b = kth_wallet_ek_public_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(kth_wallet_ek_public_equals(a, b) != 0);
    kth_wallet_ek_public_destruct(b);
    kth_wallet_ek_public_destruct(a);
}

TEST_CASE("C-API wallet::ek_public - equals / less compare distinct keys",
          "[C-API WalletEkPublic][value]") {
    kth_ek_public_mut_t a = kth_wallet_ek_public_construct_from_encoded(kEncrypted);
    kth_ek_public_mut_t b = kth_wallet_ek_public_construct_from_encoded(kEncryptedOther);
    REQUIRE(kth_wallet_ek_public_equals(a, b) == 0);

    int const a_less_b = kth_wallet_ek_public_less(a, b) != 0;
    int const b_less_a = kth_wallet_ek_public_less(b, a) != 0;
    REQUIRE(a_less_b != b_less_a);

    kth_wallet_ek_public_destruct(b);
    kth_wallet_ek_public_destruct(a);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_public - construct_from_encoded(NULL) aborts",
          "[C-API WalletEkPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_public_construct_from_encoded(NULL));
}

TEST_CASE("C-API wallet::ek_public - construct_from_value(NULL) aborts",
          "[C-API WalletEkPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_public_construct_from_value(NULL));
}

TEST_CASE("C-API wallet::ek_public - valid(NULL) aborts",
          "[C-API WalletEkPublic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_public_valid(NULL));
}
