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
#include <kth/capi/wallet/ek_token.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — BIP38 vectors lifted from the domain test suite. The
// "passphrase" prefix is the base58-checked signature for BIP38
// intermediate-passphrase tokens (create_key_pair accepts one of
// these as the `token` input).
// ---------------------------------------------------------------------------

static char const* const kEncrypted =
    "passphrasecpXbDpHuo8F7yQVcg1eQKPuX7rzGwBtEH1YSZnKbyk75x3rugZu1ci4RyF4rEn";

static char const* const kEncryptedOther =
    "passphrasecpXbDpHuo8F7x4pQXMhsJs2j7L8LTV8ujk9jGqgzUrafBeto9VrabP5SmvANvz";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_token - default construct is invalid",
          "[C-API WalletEkToken][lifecycle]") {
    kth_ek_token_mut_t a = kth_wallet_ek_token_construct_default();
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_token_valid(a) == 0);
    kth_wallet_ek_token_destruct(a);
}

TEST_CASE("C-API wallet::ek_token - destruct(NULL) is a no-op",
          "[C-API WalletEkToken][lifecycle]") {
    kth_wallet_ek_token_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Encoded string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_token - encoded round-trips through construct_from_encoded",
          "[C-API WalletEkToken][encode]") {
    kth_ek_token_mut_t a = kth_wallet_ek_token_construct_from_encoded(kEncrypted);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_ek_token_valid(a) != 0);

    char* back = kth_wallet_ek_token_encoded(a);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_token_destruct(a);
}

TEST_CASE("C-API wallet::ek_token - invalid encoded string fails to construct",
          "[C-API WalletEkToken][encode]") {
    kth_ek_token_mut_t a = kth_wallet_ek_token_construct_from_encoded("not-a-token");
    REQUIRE(a == NULL);
}

// ---------------------------------------------------------------------------
// Value (raw byte payload) round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_token - token byte payload round-trip",
          "[C-API WalletEkToken][encode]") {
    // Exercises both directions of the `kth_encrypted_token_t` ↔
    // `encrypted_token` value-struct bridge (53-byte payload).
    kth_ek_token_mut_t orig = kth_wallet_ek_token_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_token_t bytes = kth_wallet_ek_token_token(orig);

    kth_ek_token_mut_t rebuilt = kth_wallet_ek_token_construct_from_value(&bytes);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_token_valid(rebuilt) != 0);

    char* back = kth_wallet_ek_token_encoded(rebuilt);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kEncrypted) == 0);
    kth_core_destruct_string(back);

    kth_wallet_ek_token_destruct(rebuilt);
    kth_wallet_ek_token_destruct(orig);
}

TEST_CASE("C-API wallet::ek_token - construct_from_value_unsafe matches safe variant",
          "[C-API WalletEkToken][encode]") {
    kth_ek_token_mut_t orig = kth_wallet_ek_token_construct_from_encoded(kEncrypted);
    REQUIRE(orig != NULL);
    kth_encrypted_token_t bytes = kth_wallet_ek_token_token(orig);

    kth_ek_token_mut_t rebuilt =
        kth_wallet_ek_token_construct_from_value_unsafe(bytes.data);
    REQUIRE(rebuilt != NULL);
    REQUIRE(kth_wallet_ek_token_equals(orig, rebuilt) != 0);

    kth_wallet_ek_token_destruct(rebuilt);
    kth_wallet_ek_token_destruct(orig);
}

// ---------------------------------------------------------------------------
// Copy / equality / ordering
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_token - copy preserves value equality",
          "[C-API WalletEkToken][value]") {
    kth_ek_token_mut_t a = kth_wallet_ek_token_construct_from_encoded(kEncrypted);
    kth_ek_token_mut_t b = kth_wallet_ek_token_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(kth_wallet_ek_token_equals(a, b) != 0);
    kth_wallet_ek_token_destruct(b);
    kth_wallet_ek_token_destruct(a);
}

TEST_CASE("C-API wallet::ek_token - equals / less compare distinct tokens",
          "[C-API WalletEkToken][value]") {
    kth_ek_token_mut_t a = kth_wallet_ek_token_construct_from_encoded(kEncrypted);
    kth_ek_token_mut_t b = kth_wallet_ek_token_construct_from_encoded(kEncryptedOther);
    REQUIRE(kth_wallet_ek_token_equals(a, b) == 0);

    int const a_less_b = kth_wallet_ek_token_less(a, b) != 0;
    int const b_less_a = kth_wallet_ek_token_less(b, a) != 0;
    REQUIRE(a_less_b != b_less_a);

    kth_wallet_ek_token_destruct(b);
    kth_wallet_ek_token_destruct(a);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::ek_token - construct_from_encoded(NULL) aborts",
          "[C-API WalletEkToken][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_token_construct_from_encoded(NULL));
}

TEST_CASE("C-API wallet::ek_token - construct_from_value(NULL) aborts",
          "[C-API WalletEkToken][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_token_construct_from_value(NULL));
}

TEST_CASE("C-API wallet::ek_token - valid(NULL) aborts",
          "[C-API WalletEkToken][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_ek_token_valid(NULL));
}
