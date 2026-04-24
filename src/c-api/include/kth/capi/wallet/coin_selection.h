// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_COIN_SELECTION_H_
#define KTH_CAPI_WALLET_COIN_SELECTION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>
#include <kth/capi/wallet/coin_selection_strategy.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

/**
 * @param category Borrowed input; must be non-null. Read during the call; ownership of `category` stays with the caller.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos(kth_utxo_list_const_t available_utxos, uint64_t amount, kth_size_t outputs_size, kth_hash_t const* category, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/**
 * @warning `category` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos_unsafe(kth_utxo_list_const_t available_utxos, uint64_t amount, kth_size_t outputs_size, uint8_t const* category, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/**
 * @param category Borrowed input; must be non-null. Read during the call; ownership of `category` stays with the caller.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos_send_all(kth_utxo_list_const_t available_utxos, kth_size_t outputs_size, kth_hash_t const* category, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/**
 * @warning `category` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos_send_all_unsafe(kth_utxo_list_const_t available_utxos, kth_size_t outputs_size, uint8_t const* category, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/**
 * @param token_category Borrowed input; must be non-null. Read during the call; ownership of `token_category` stays with the caller.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos_both(kth_utxo_list_const_t available_utxos, uint64_t bch_amount, kth_hash_t const* token_category, uint64_t token_amount, kth_size_t outputs_size, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/**
 * @warning `token_category` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_coin_selection_result_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_coin_selection_result_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_select_utxos_both_unsafe(kth_utxo_list_const_t available_utxos, uint64_t bch_amount, uint8_t const* token_category, uint64_t token_amount, kth_size_t outputs_size, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out);

/** @return Owned `kth_double_list_mut_t`. Caller must release with `kth_core_double_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_double_list_mut_t kth_wallet_coin_selection_make_change_ratios(kth_size_t change_count);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_COIN_SELECTION_H_
