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

#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/token_data.h>
#include <kth/capi/chain/utxo.h>
#include <kth/capi/chain/utxo_list.h>
#include <kth/capi/double_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/coin_selection.h>
#include <kth/capi/wallet/coin_selection_result.h>
#include <kth/capi/wallet/coin_selection_strategy.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// BCH selection uses the canonical all-zero hash (null_hash) as the
// "this is BCH, not a token" sentinel — matches the C++ `bch_id`
// constant in coin_selection.hpp.
static kth_hash_t const kBchCategory = {{ 0 }};

static kth_hash_t const kTokenCategory = {{
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb
}};

static kth_hash_t const kPrevTxid = {{
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
}};

// Build a UTXO list of pure-BCH outputs. Caller owns the returned
// list and must release with kth_chain_utxo_list_destruct.
static kth_utxo_list_mut_t make_bch_utxos(uint64_t const* amounts, int count) {
    kth_utxo_list_mut_t list = kth_chain_utxo_list_construct_default();
    for (int i = 0; i < count; ++i) {
        kth_output_point_mut_t op =
            kth_chain_output_point_construct_from_hash_index(&kPrevTxid, (uint32_t)i);
        kth_utxo_mut_t u = kth_chain_utxo_construct(op, amounts[i], NULL);
        kth_chain_utxo_list_push_back(list, u);
        kth_chain_utxo_destruct(u);
        kth_chain_output_point_destruct(op);
    }
    return list;
}

// ---------------------------------------------------------------------------
// result — lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection_result - destruct(NULL) is a no-op",
          "[C-API WalletCoinSelection][result]") {
    kth_wallet_coin_selection_result_destruct(NULL);
}

// ---------------------------------------------------------------------------
// select_utxos — BCH, clean strategy
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection - select_utxos picks enough BCH to cover amount",
          "[C-API WalletCoinSelection][select]") {
    // Ask for 1500 sats from a set of {1000, 2000, 3000} sats. The
    // picker must select at least enough to cover the amount plus the
    // per-input fee estimate; the exact choice depends on strategy,
    // but total_selected_bch must be >= amount.
    uint64_t amounts[] = { 1000u, 2000u, 3000u };
    kth_utxo_list_mut_t utxos = make_bch_utxos(amounts, 3);

    kth_coin_selection_result_mut_t out = NULL;
    kth_error_code_t rc = kth_wallet_coin_selection_select_utxos(
        utxos, 1500u, 1u, &kBchCategory,
        kth_coin_selection_strategy_clean, &out);
    REQUIRE(rc == kth_ec_success);
    REQUIRE(out != NULL);

    REQUIRE(kth_wallet_coin_selection_result_total_selected_bch(out) >= 1500u);
    REQUIRE(kth_wallet_coin_selection_result_total_selected_token(out) == 0u);
    REQUIRE(kth_wallet_coin_selection_result_estimated_size(out) > 0u);

    kth_wallet_coin_selection_result_destruct(out);
    kth_chain_utxo_list_destruct(utxos);
}

TEST_CASE("C-API wallet::coin_selection - select_utxos fails when funds are insufficient",
          "[C-API WalletCoinSelection][select]") {
    // Only 100 sats available; asking for 1000 must fail cleanly with
    // a non-success error code and leave `out` untouched (NULL).
    uint64_t amounts[] = { 100u };
    kth_utxo_list_mut_t utxos = make_bch_utxos(amounts, 1);

    kth_coin_selection_result_mut_t out = NULL;
    kth_error_code_t rc = kth_wallet_coin_selection_select_utxos(
        utxos, 1000u, 1u, &kBchCategory,
        kth_coin_selection_strategy_clean, &out);
    REQUIRE(rc != kth_ec_success);
    REQUIRE(out == NULL);

    kth_chain_utxo_list_destruct(utxos);
}

// ---------------------------------------------------------------------------
// select_utxos_send_all
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection - send_all sums every matching UTXO",
          "[C-API WalletCoinSelection][select]") {
    // In "send all BCH" mode the picker takes every pure-BCH UTXO;
    // total_selected_bch is the sum of the inputs minus the fee the
    // tx would need, but must match the raw input total here since
    // send_all doesn't carve out a fee from the `total_selected`
    // reporting (that is handled at tx-template time).
    uint64_t amounts[] = { 1000u, 2000u, 3000u };
    kth_utxo_list_mut_t utxos = make_bch_utxos(amounts, 3);

    kth_coin_selection_result_mut_t out = NULL;
    kth_error_code_t rc = kth_wallet_coin_selection_select_utxos_send_all(
        utxos, 1u, &kBchCategory, &out);
    REQUIRE(rc == kth_ec_success);
    REQUIRE(out != NULL);
    REQUIRE(kth_wallet_coin_selection_result_total_selected_bch(out) == 6000u);

    kth_wallet_coin_selection_result_destruct(out);
    kth_chain_utxo_list_destruct(utxos);
}

// ---------------------------------------------------------------------------
// select_utxos_both — asking for a token when none exists must fail
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection - select_utxos_both fails when token absent",
          "[C-API WalletCoinSelection][select]") {
    // With only pure-BCH UTXOs available, asking for any amount of a
    // specific token must fail — there is nothing to select from for
    // the token half of the request.
    uint64_t amounts[] = { 10000u };
    kth_utxo_list_mut_t utxos = make_bch_utxos(amounts, 1);

    kth_coin_selection_result_mut_t out = NULL;
    kth_error_code_t rc = kth_wallet_coin_selection_select_utxos_both(
        utxos, 1000u, &kTokenCategory, 500u, 2u,
        kth_coin_selection_strategy_clean, &out);
    REQUIRE(rc != kth_ec_success);
    REQUIRE(out == NULL);

    kth_chain_utxo_list_destruct(utxos);
}

// ---------------------------------------------------------------------------
// make_change_ratios — returns a list of `count` doubles summing to ~1.0
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection - make_change_ratios produces a list of the requested size",
          "[C-API WalletCoinSelection][ratios]") {
    // make_change_ratios is a deterministic utility: for `count = N`
    // it returns N doubles that sum to 1.0 (within floating-point
    // tolerance). The randomisation is inside the distribution, not
    // the element count — so the count check is stable.
    kth_double_list_mut_t ratios = kth_wallet_coin_selection_make_change_ratios(5u);
    REQUIRE(ratios != NULL);
    REQUIRE(kth_core_double_list_count(ratios) == 5u);

    double sum = 0.0;
    for (kth_size_t i = 0; i < 5u; ++i) {
        sum += kth_core_double_list_nth(ratios, i);
    }
    // Allow a little FP slack; the domain is not guaranteed to hit
    // exactly 1.0 for every seed, but it is normalised to the unit
    // interval and the sum is expected to be very close.
    REQUIRE(sum > 0.999);
    REQUIRE(sum < 1.001);

    kth_core_double_list_destruct(ratios);
}

TEST_CASE("C-API wallet::coin_selection - make_change_ratios(0) returns empty list",
          "[C-API WalletCoinSelection][ratios]") {
    kth_double_list_mut_t ratios = kth_wallet_coin_selection_make_change_ratios(0u);
    REQUIRE(ratios != NULL);
    REQUIRE(kth_core_double_list_count(ratios) == 0u);
    kth_core_double_list_destruct(ratios);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::coin_selection - select_utxos(NULL utxos) aborts",
          "[C-API WalletCoinSelection][precondition]") {
    kth_coin_selection_result_mut_t out = NULL;
    KTH_EXPECT_ABORT(kth_wallet_coin_selection_select_utxos(
        NULL, 1u, 1u, &kBchCategory,
        kth_coin_selection_strategy_clean, &out));
}

TEST_CASE("C-API wallet::coin_selection - select_utxos(NULL category) aborts",
          "[C-API WalletCoinSelection][precondition]") {
    kth_utxo_list_mut_t utxos = kth_chain_utxo_list_construct_default();
    kth_coin_selection_result_mut_t out = NULL;
    KTH_EXPECT_ABORT(kth_wallet_coin_selection_select_utxos(
        utxos, 1u, 1u, NULL,
        kth_coin_selection_strategy_clean, &out));
    kth_chain_utxo_list_destruct(utxos);
}

TEST_CASE("C-API wallet::coin_selection - select_utxos(NULL out) aborts",
          "[C-API WalletCoinSelection][precondition]") {
    kth_utxo_list_mut_t utxos = kth_chain_utxo_list_construct_default();
    KTH_EXPECT_ABORT(kth_wallet_coin_selection_select_utxos(
        utxos, 1u, 1u, &kBchCategory,
        kth_coin_selection_strategy_clean, NULL));
    kth_chain_utxo_list_destruct(utxos);
}
