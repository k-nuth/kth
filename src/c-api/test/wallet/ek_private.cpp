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
#include <kth/capi/wallet/ek_private.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — BIP38 vectors lifted from the domain test suite. The
// "6P..." prefix is the base58-checked signature for encrypted private
// keys (ek_private is the wire-format wrapper used for display).
// ---------------------------------------------------------------------------

static char const* const kEncrypted =
    "6PRVWUbkzzsbcVac2qwfssoUJAN1Xhrg6bNk8J7Nzm5H7kxEbn2Nh2ZoGg";

static char const* const kEncryptedOther =
    "6PRNFFkZc2NZ6dJqFfhRoFNMR9Lnyj7dYGrzdgXXVMXcxoKTePPX1dWByq";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_private - default construct is invalid",
          "[C-API WalletEkPrivate][lifecycle]") {
    // Matches the domain default ctor: no payload, `valid_ == false`.
    // The handle itself is non-null (the allocation succeeded), but
    // `valid()` returns 0 so callers know not to trust the state.
    kth_ek_private_mut_t a = kth_wallet_ek_private_construct_default();
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_private_valid(a) == 0);
    kth_wallet_ek_private_destruct(a);
}

TEST_CASE("C-API wallet::ek_private - destruct(NULL) is a no-op",
          "[C-API WalletEkPrivate][lifecycle]") {
    kth_wallet_ek_private_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Encoded string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_private - encoded round-trips through construct_from_encoded",
          "[C-API WalletEkPrivate][encode]") {
    // Parse a known-good BIP38 vector and re-serialise; the result
    // must match byte-for-byte. A regression in base58-check framing
    // would diverge here.
    kth_ek_private_mut_t a = kth_wallet_ek_private_construct_from_encoded(kEncrypted);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_private_valid(a) != 0);

    char* back = kth_wallet_ek_private_encoded(a);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_private_destruct(a);
}

TEST_CASE("C-API wallet::ek_private - invalid encoded string fails to construct",
          "[C-API WalletEkPrivate][encode]") {
    // Anything that isn't valid BIP38 base58-check fails the domain
    // `from_string` factory and `leak_if_valid` maps that to NULL.
    kth_ek_private_mut_t a = kth_wallet_ek_private_construct_from_encoded("not-a-key");
    REQUIRE(a == NULL);
}

// ---------------------------------------------------------------------------
// Value (raw byte payload) round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_private - private_key byte payload round-trip",
          "[C-API WalletEkPrivate][encode]") {
    // encoded → ek_private → raw 43-byte payload → new ek_private →
    // encoded. This exercises both directions of the value-struct
    // bridge (`kth_encrypted_private_t` ↔ `encrypted_private`).
    kth_ek_private_mut_t orig = kth_wallet_ek_private_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_private_t bytes = kth_wallet_ek_private_private_key(orig);

    kth_ek_private_mut_t rebuilt = kth_wallet_ek_private_construct_from_value(&bytes);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_private_valid(rebuilt) != 0);

    char* back = kth_wallet_ek_private_encoded(rebuilt);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_private_destruct(rebuilt);
    kth_wallet_ek_private_destruct(orig);
}

TEST_CASE("C-API wallet::ek_private - construct_from_value_unsafe matches safe variant",
          "[C-API WalletEkPrivate][encode]") {
    // Equivalent call path for callers that can't pass a struct by
    // pointer (some FFIs can only hand over a raw `uint8_t*`). Both
    // must produce equal wrappers over the same payload.
    kth_ek_private_mut_t orig = kth_wallet_ek_private_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_private_t bytes = kth_wallet_ek_private_private_key(orig);

    kth_ek_private_mut_t rebuilt =
        kth_wallet_ek_private_construct_from_value_unsafe(bytes.data);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_private_equals(orig, rebuilt) != 0);

    kth_wallet_ek_private_destruct(rebuilt);
    kth_wallet_ek_private_destruct(orig);
}

// ---------------------------------------------------------------------------
// Copy / equality / ordering
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_private - copy preserves value equality",
          "[C-API WalletEkPrivate][value]") {
    kth_ek_private_mut_t a = kth_wallet_ek_private_construct_from_encoded(kEncrypted);
    kth_ek_private_mut_t b = kth_wallet_ek_private_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(kth_wallet_ek_private_equals(a, b) != 0);
    kth_wallet_ek_private_destruct(b);
    kth_wallet_ek_private_destruct(a);
}

TEST_CASE("C-API wallet::ek_private - equals / less compare distinct keys",
          "[C-API WalletEkPrivate][value]") {
    // Two different BIP38 keys must not compare equal; `less` is a
    // total ordering so exactly one of `a<b` / `b<a` is non-zero.
    kth_ek_private_mut_t a = kth_wallet_ek_private_construct_from_encoded(kEncrypted);
    kth_ek_private_mut_t b = kth_wallet_ek_private_construct_from_encoded(kEncryptedOther);
    REQUIRE(kth_wallet_ek_private_equals(a, b) == 0);

    int const a_less_b = kth_wallet_ek_private_less(a, b) != 0;
    int const b_less_a = kth_wallet_ek_private_less(b, a) != 0;
    REQUIRE(a_less_b != b_less_a);

    kth_wallet_ek_private_destruct(b);
    kth_wallet_ek_private_destruct(a);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_private - construct_from_encoded(NULL) aborts",
          "[C-API WalletEkPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_private_construct_from_encoded(NULL));
}

TEST_CASE("C-API wallet::ek_private - construct_from_value(NULL) aborts",
          "[C-API WalletEkPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_private_construct_from_value(NULL));
}

TEST_CASE("C-API wallet::ek_private - valid(NULL) aborts",
          "[C-API WalletEkPrivate][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_private_valid(NULL));
}
