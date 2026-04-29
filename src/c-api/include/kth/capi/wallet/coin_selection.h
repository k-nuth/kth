// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_COIN_SELECTION_H_
#define KTH_CAPI_WALLET_COIN_SELECTION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>
#include <kth/capi/wallet/coin_selection_algorithm.h>
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

/**
 * Build an unsigned BCH-only transaction template by selecting UTXOs and
 * distributing change across `change_addresses` using `change_ratios`.
 *
 * @param destination_address Borrowed; must be non-null.
 * @param change_addresses Borrowed; must be non-null. Empty list is rejected.
 * @param change_ratios Borrowed; must be non-null and have the same length as
 *                     `change_addresses`. Values must sum to 1.0.
 * @param[out] out_tx        On success, owned `kth_transaction_mut_t` (release via `kth_chain_transaction_destruct`). Untouched on error.
 * @param[out] out_indices   On success, owned `kth_u32_list_mut_t` of selected UTXO indices (release via `kth_core_u32_list_destruct`). Untouched on error.
 * @param[out] out_addresses On success, owned `kth_payment_address_list_mut_t` of one address per output (release via `kth_wallet_payment_address_list_destruct`). Untouched on error.
 * @param[out] out_amounts   On success, owned `kth_u64_list_mut_t` of one BCH amount per output (release via `kth_core_u64_list_destruct`). Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_create_tx_template(
    kth_utxo_list_const_t available_utxos,
    uint64_t amount_to_send,
    kth_payment_address_const_t destination_address,
    kth_payment_address_list_const_t change_addresses,
    kth_double_list_const_t change_ratios,
    kth_coin_selection_algorithm_t selection_algo,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_u32_list_mut_t* out_indices,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts);

/**
 * Same as `kth_wallet_coin_selection_create_tx_template` but generates
 * random change ratios internally (equivalent to calling
 * `kth_wallet_coin_selection_make_change_ratios` and passing the result).
 *
 * @param destination_address Borrowed; must be non-null.
 * @param change_addresses Borrowed; must be non-null. Empty list is rejected.
 * @param[out] out_tx        On success, owned `kth_transaction_mut_t` (release via `kth_chain_transaction_destruct`). Untouched on error.
 * @param[out] out_indices   On success, owned `kth_u32_list_mut_t` of selected UTXO indices (release via `kth_core_u32_list_destruct`). Untouched on error.
 * @param[out] out_addresses On success, owned `kth_payment_address_list_mut_t` of one address per output (release via `kth_wallet_payment_address_list_destruct`). Untouched on error.
 * @param[out] out_amounts   On success, owned `kth_u64_list_mut_t` of one BCH amount per output (release via `kth_core_u64_list_destruct`). Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_create_tx_template_default_ratios(
    kth_utxo_list_const_t available_utxos,
    uint64_t amount_to_send,
    kth_payment_address_const_t destination_address,
    kth_payment_address_list_const_t change_addresses,
    kth_coin_selection_algorithm_t selection_algo,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_u32_list_mut_t* out_indices,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts);

/**
 * Build an unsigned transaction that splits "dirty" token UTXOs into clean
 * ones (token-only output at dust BCH + a separate BCH-only output for the
 * freed excess). Only fungible tokens are supported.
 *
 * @param outpoints_to_split Borrowed; must be non-null. The dirty outpoints to clean.
 * @param available_utxos Borrowed; must be non-null. Used to look up the outpoints.
 * @param destination_address Borrowed; must be non-null.
 * @param[out] out_tx        On success, owned `kth_transaction_mut_t` (release via `kth_chain_transaction_destruct`). Untouched on error.
 * @param[out] out_addresses On success, owned `kth_payment_address_list_mut_t` of one address per output (release via `kth_wallet_payment_address_list_destruct`). Untouched on error.
 * @param[out] out_amounts   On success, owned `kth_u64_list_mut_t` of one BCH amount per output (release via `kth_core_u64_list_destruct`). Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_coin_selection_create_token_split_tx_template(
    kth_output_point_list_const_t outpoints_to_split,
    kth_utxo_list_const_t available_utxos,
    kth_payment_address_const_t destination_address,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_COIN_SELECTION_H_
