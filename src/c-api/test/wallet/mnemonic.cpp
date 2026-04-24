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
#include <kth/capi/wallet/language.h>
#include <kth/capi/wallet/mnemonic.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — BIP39 test vector (English, 128-bit entropy).
// ---------------------------------------------------------------------------

// All-zero entropy → canonical first BIP39 vector:
//   "abandon abandon abandon abandon abandon abandon
//    abandon abandon abandon abandon abandon about"
static uint8_t const kZeroEntropy128[16] = { 0 };

// 160-bit zeros → 15-word mnemonic (BIP39 word count = entropy_bits / 32 * 3).
static uint8_t const kZeroEntropy160[20] = { 0 };

// ---------------------------------------------------------------------------
// Language factories — borrowed views into static storage
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::language - built-in factories return non-null handles",
          "[C-API WalletLanguage]") {
    // All 10 language accessors must return borrowed views into the
    // compiled-in `kth::domain::wallet::language::*` globals. NULL
    // here would mean the domain linkage broke.
    REQUIRE(kth_wallet_language_en() != NULL);
    REQUIRE(kth_wallet_language_es() != NULL);
    REQUIRE(kth_wallet_language_ja() != NULL);
    REQUIRE(kth_wallet_language_it() != NULL);
    REQUIRE(kth_wallet_language_fr() != NULL);
    REQUIRE(kth_wallet_language_cs() != NULL);
    REQUIRE(kth_wallet_language_ru() != NULL);
    REQUIRE(kth_wallet_language_uk() != NULL);
    REQUIRE(kth_wallet_language_zh_Hans() != NULL);
    REQUIRE(kth_wallet_language_zh_Hant() != NULL);
    REQUIRE(kth_wallet_language_all() != NULL);
}

TEST_CASE("C-API wallet::language - each factory returns a distinct dictionary",
          "[C-API WalletLanguage]") {
    // The handles borrow into 10 separate static `dictionary`
    // globals — no two should alias. A regression that pointed them
    // at the same wordlist (e.g. a copy-paste mistake in the domain
    // factories) would collapse `validate` cross-language as well.
    REQUIRE(kth_wallet_language_en() != kth_wallet_language_es());
    REQUIRE(kth_wallet_language_en() != kth_wallet_language_ja());
    REQUIRE(kth_wallet_language_zh_Hans() != kth_wallet_language_zh_Hant());
}

// ---------------------------------------------------------------------------
// create_mnemonic
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::mnemonic - 128-bit entropy yields 12-word mnemonic",
          "[C-API WalletMnemonic][create]") {
    // BIP39: 128 bits of entropy → 12 words (11 from entropy + 1
    // checksum word derived from the first 4 bits of SHA-256(entropy)).
    kth_string_list_mut_t words = kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy128, sizeof(kZeroEntropy128), kth_wallet_language_en());
    REQUIRE(words != NULL);
    REQUIRE(kth_core_string_list_count(words) == 12u);

    // Canonical BIP39 all-zero-entropy checksum word is "about".
    char const* last = kth_core_string_list_nth(words, 11u);
    REQUIRE(last != NULL);
    REQUIRE(strcmp(last, "about") == 0);

    kth_core_string_list_destruct(words);
}

TEST_CASE("C-API wallet::mnemonic - 160-bit entropy yields 15-word mnemonic",
          "[C-API WalletMnemonic][create]") {
    kth_string_list_mut_t words = kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy160, sizeof(kZeroEntropy160), kth_wallet_language_en());
    REQUIRE(words != NULL);
    REQUIRE(kth_core_string_list_count(words) == 15u);
    kth_core_string_list_destruct(words);
}

// ---------------------------------------------------------------------------
// validate_mnemonic — against a specific dictionary
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::mnemonic - validate accepts a freshly-created mnemonic",
          "[C-API WalletMnemonic][validate]") {
    // Round-trip: create → validate under the same language. Any
    // drift in wordlist handling between create and validate would
    // surface here.
    kth_string_list_mut_t words = kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy128, sizeof(kZeroEntropy128), kth_wallet_language_en());
    REQUIRE(words != NULL);
    REQUIRE(kth_wallet_mnemonic_validate_mnemonic_dictionary(
        words, kth_wallet_language_en()) != 0);
    kth_core_string_list_destruct(words);
}

TEST_CASE("C-API wallet::mnemonic - validate rejects under the wrong language",
          "[C-API WalletMnemonic][validate]") {
    // English words won't parse under the Spanish dictionary; the
    // lookup fails before the checksum is even considered.
    kth_string_list_mut_t words = kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy128, sizeof(kZeroEntropy128), kth_wallet_language_en());
    REQUIRE(words != NULL);
    REQUIRE(kth_wallet_mnemonic_validate_mnemonic_dictionary(
        words, kth_wallet_language_es()) == 0);
    kth_core_string_list_destruct(words);
}

TEST_CASE("C-API wallet::mnemonic - validate rejects a bad checksum",
          "[C-API WalletMnemonic][validate]") {
    // Build a list where every word is a valid BIP39 English word but
    // the checksum is wrong. `abandon` × 12 is only valid when the
    // entropy is all zero; swapping the last word for any other valid
    // English BIP39 word breaks the checksum.
    kth_string_list_mut_t broken = kth_core_string_list_construct_default();
    for (int i = 0; i < 12; ++i) {
        kth_core_string_list_push_back(broken, "abandon");  // correct
                                                            // checksum
                                                            // word is
                                                            // "about"
    }

    REQUIRE(kth_wallet_mnemonic_validate_mnemonic_dictionary(
        broken, kth_wallet_language_en()) == 0);

    kth_core_string_list_destruct(broken);
}

// ---------------------------------------------------------------------------
// validate_mnemonic — against every built-in dictionary
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::mnemonic - validate_any accepts any built-in language",
          "[C-API WalletMnemonic][validate]") {
    // A valid Spanish-language mnemonic must be accepted by the
    // dictionary_list overload without the caller specifying Spanish.
    kth_string_list_mut_t words = kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy128, sizeof(kZeroEntropy128), kth_wallet_language_es());
    REQUIRE(words != NULL);
    REQUIRE(kth_wallet_mnemonic_validate_mnemonic_dictionary_list(
        words, kth_wallet_language_all()) != 0);
    kth_core_string_list_destruct(words);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::mnemonic - create null entropy with non-zero size aborts",
          "[C-API WalletMnemonic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_mnemonic_create_mnemonic(
        NULL, 16u, kth_wallet_language_en()));
}

TEST_CASE("C-API wallet::mnemonic - create null lexicon aborts",
          "[C-API WalletMnemonic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_mnemonic_create_mnemonic(
        kZeroEntropy128, sizeof(kZeroEntropy128), NULL));
}

TEST_CASE("C-API wallet::mnemonic - validate null mnemonic aborts",
          "[C-API WalletMnemonic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_mnemonic_validate_mnemonic_dictionary(
        NULL, kth_wallet_language_en()));
}

TEST_CASE("C-API wallet::mnemonic - validate_any null mnemonic aborts",
          "[C-API WalletMnemonic][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_mnemonic_validate_mnemonic_dictionary_list(
        NULL, kth_wallet_language_all()));
}
