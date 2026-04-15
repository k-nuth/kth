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
#include <kth/capi/string_list.h>
#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/wallet/wallet_data.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API WalletData - destruct null is safe", "[C-API WalletData]") {
    kth_wallet_wallet_data_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

TEST_CASE("C-API WalletData - create with English default returns a handle",
          "[C-API WalletData]") {
    kth_wallet_data_mut_t wd = NULL;
    kth_error_code_t ec = kth_wallet_create(
        "testpassword", "testpassphrase", &wd);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(wd != NULL);

    // mnemonics: 24-word BIP39 seed phrase (256-bit entropy default).
    kth_string_list_const_t mnemonics = kth_wallet_wallet_data_mnemonics(wd);
    REQUIRE(mnemonics != NULL);
    REQUIRE(kth_core_string_list_count(mnemonics) == 24u);

    // xpub: a non-null borrowed view.
    kth_hd_public_const_t xpub = kth_wallet_wallet_data_xpub(wd);
    REQUIRE(xpub != NULL);
    REQUIRE(kth_wallet_hd_public_valid(xpub) != 0);

    // encrypted_seed: fixed 96-byte struct (salt + iv + encrypted long_hash).
    kth_encrypted_seed_t seed = kth_wallet_wallet_data_encrypted_seed(wd);
    // Accessible; content is pseudo-random — not meaningful to assert a value.
    (void)seed;

    kth_wallet_wallet_data_destruct(wd);
}

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

TEST_CASE("C-API WalletData - copy preserves mnemonics count and xpub validity",
          "[C-API WalletData]") {
    kth_wallet_data_mut_t original = NULL;
    REQUIRE(kth_wallet_create(
        "pwd", "passphrase", &original) == kth_ec_success);

    kth_wallet_data_mut_t copy = kth_wallet_wallet_data_copy(original);
    REQUIRE(copy != NULL);
    REQUIRE(copy != original);  // different heap allocations

    REQUIRE(kth_core_string_list_count(kth_wallet_wallet_data_mnemonics(copy))
            == kth_core_string_list_count(kth_wallet_wallet_data_mnemonics(original)));
    REQUIRE(kth_wallet_hd_public_valid(kth_wallet_wallet_data_xpub(copy)) != 0);

    kth_wallet_wallet_data_destruct(copy);
    kth_wallet_wallet_data_destruct(original);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API WalletData - set_mnemonics replaces the word list",
          "[C-API WalletData]") {
    kth_wallet_data_mut_t wd = NULL;
    REQUIRE(kth_wallet_create(
        "p", "p", &wd) == kth_ec_success);

    kth_string_list_mut_t replacement = kth_core_string_list_construct_default();
    kth_core_string_list_push_back(replacement, "word-one");
    kth_core_string_list_push_back(replacement, "word-two");

    kth_wallet_wallet_data_set_mnemonics(wd, replacement);
    REQUIRE(kth_core_string_list_count(kth_wallet_wallet_data_mnemonics(wd)) == 2u);

    kth_core_string_list_destruct(replacement);
    kth_wallet_wallet_data_destruct(wd);
}

TEST_CASE("C-API WalletData - set_encrypted_seed_unsafe round-trip",
          "[C-API WalletData]") {
    kth_wallet_data_mut_t wd = NULL;
    REQUIRE(kth_wallet_create(
        "p", "p", &wd) == kth_ec_success);

    uint8_t replacement[96];
    for (int i = 0; i < 96; ++i) replacement[i] = (uint8_t)(i & 0xff);

    kth_wallet_wallet_data_set_encrypted_seed_unsafe(wd, replacement);

    kth_encrypted_seed_t got = kth_wallet_wallet_data_encrypted_seed(wd);
    REQUIRE(memcmp(got.hash, replacement, 96) == 0);

    kth_wallet_wallet_data_destruct(wd);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API WalletData - copy null aborts",
          "[C-API WalletData][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_wallet_data_copy(NULL));
}

TEST_CASE("C-API WalletData - mnemonics getter null aborts",
          "[C-API WalletData][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_wallet_data_mnemonics(NULL));
}

TEST_CASE("C-API WalletData - create null out aborts",
          "[C-API WalletData][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_create("p", "p", NULL));
}

TEST_CASE("C-API WalletData - create null password aborts",
          "[C-API WalletData][precondition]") {
    kth_wallet_data_mut_t wd = NULL;
    KTH_EXPECT_ABORT(kth_wallet_create(NULL, "p", &wd));
}

TEST_CASE("C-API WalletData - create null passphrase aborts",
          "[C-API WalletData][precondition]") {
    kth_wallet_data_mut_t wd = NULL;
    KTH_EXPECT_ABORT(kth_wallet_create("p", NULL, &wd));
}

TEST_CASE("C-API WalletData - create non-null *out aborts",
          "[C-API WalletData][precondition]") {
    kth_wallet_data_mut_t already = NULL;
    REQUIRE(kth_wallet_create("p", "p", &already) == kth_ec_success);
    KTH_EXPECT_ABORT(kth_wallet_create("p", "p", &already));
    kth_wallet_wallet_data_destruct(already);
}

TEST_CASE("C-API WalletData - set_encrypted_seed_unsafe null aborts",
          "[C-API WalletData][precondition]") {
    kth_wallet_data_mut_t wd = NULL;
    REQUIRE(kth_wallet_create("p", "p", &wd) == kth_ec_success);
    KTH_EXPECT_ABORT(kth_wallet_wallet_data_set_encrypted_seed_unsafe(wd, NULL));
    kth_wallet_wallet_data_destruct(wd);
}
