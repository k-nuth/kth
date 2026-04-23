// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_CASHTOKEN_MINTING_H_
#define KTH_CAPI_WALLET_CASHTOKEN_MINTING_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/token_capability.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Opaque handles — one per C++ params/result struct.
// ---------------------------------------------------------------------------
//
// C mirrors the `kth::domain::wallet::cashtoken` C++ API. Each C++ struct
// maps to an opaque handle (`kth_cashtoken_<type>_mut_t`) with
// `construct_default` / `destruct` lifecycle functions plus per-field
// setters/getters. Builders take a params handle and return either a
// result handle via out-parameter or an error code.
//
// Type aliases (all `void*` at the ABI level, see helpers.hpp):
typedef void*       kth_cashtoken_prepare_genesis_params_mut_t;
typedef void const* kth_cashtoken_prepare_genesis_params_const_t;
typedef void*       kth_cashtoken_prepare_genesis_result_mut_t;
typedef void const* kth_cashtoken_prepare_genesis_result_const_t;

typedef void*       kth_cashtoken_nft_spec_mut_t;
typedef void const* kth_cashtoken_nft_spec_const_t;
typedef void*       kth_cashtoken_nft_mint_request_mut_t;
typedef void const* kth_cashtoken_nft_mint_request_const_t;
typedef void*       kth_cashtoken_nft_mint_request_list_mut_t;
typedef void const* kth_cashtoken_nft_mint_request_list_const_t;
typedef void*       kth_cashtoken_nft_collection_item_mut_t;
typedef void const* kth_cashtoken_nft_collection_item_const_t;
typedef void*       kth_cashtoken_nft_collection_item_list_mut_t;
typedef void const* kth_cashtoken_nft_collection_item_list_const_t;

typedef void*       kth_cashtoken_token_genesis_params_mut_t;
typedef void const* kth_cashtoken_token_genesis_params_const_t;
typedef void*       kth_cashtoken_token_genesis_result_mut_t;
typedef void const* kth_cashtoken_token_genesis_result_const_t;

typedef void*       kth_cashtoken_token_mint_params_mut_t;
typedef void const* kth_cashtoken_token_mint_params_const_t;
typedef void*       kth_cashtoken_token_mint_result_mut_t;
typedef void const* kth_cashtoken_token_mint_result_const_t;

typedef void*       kth_cashtoken_token_transfer_params_mut_t;
typedef void const* kth_cashtoken_token_transfer_params_const_t;
typedef void*       kth_cashtoken_token_burn_params_mut_t;
typedef void const* kth_cashtoken_token_burn_params_const_t;
typedef void*       kth_cashtoken_token_tx_result_mut_t;
typedef void const* kth_cashtoken_token_tx_result_const_t;

typedef void*       kth_cashtoken_ft_params_mut_t;
typedef void const* kth_cashtoken_ft_params_const_t;

typedef void*       kth_cashtoken_nft_collection_params_mut_t;
typedef void const* kth_cashtoken_nft_collection_params_const_t;
typedef void*       kth_cashtoken_nft_collection_result_mut_t;
typedef void const* kth_cashtoken_nft_collection_result_const_t;


// ---------------------------------------------------------------------------
// encode_nft_number
// ---------------------------------------------------------------------------

/**
 * Encode an integer as a minimally-serialised Bitcoin Script number
 * (VM-number) suitable for use as an NFT commitment.
 *
 * @param value      Integer to encode.
 * @param out_data   Receives an owned byte array (caller must release with
 *                   `kth_core_destruct_array`). Untouched on error.
 * @param out_size   Receives the size of `*out_data`. Zero is a valid
 *                   encoding (the empty byte string for `value == 0`).
 * @return `kth_ec_success` on success, otherwise a VM-number range error
 *         (notably when `value == INT64_MIN`, whose negation is UB).
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_encode_nft_number(
    int64_t value,
    uint8_t** out_data,
    kth_size_t* out_size);


// ---------------------------------------------------------------------------
// Output factories
// ---------------------------------------------------------------------------

/** @return Owned `kth_output_mut_t`. Caller must release with `kth_chain_output_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_wallet_cashtoken_create_ft_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    uint64_t ft_amount,
    uint64_t satoshis);

/** @return Owned `kth_output_mut_t`. Caller must release with `kth_chain_output_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_wallet_cashtoken_create_nft_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    uint64_t satoshis);

/** @return Owned `kth_output_mut_t`. Caller must release with `kth_chain_output_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_wallet_cashtoken_create_combined_token_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    uint64_t ft_amount,
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    uint64_t satoshis);


// ---------------------------------------------------------------------------
// nft_spec
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_spec_mut_t kth_wallet_cashtoken_nft_spec_construct(
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n);

KTH_EXPORT
void kth_wallet_cashtoken_nft_spec_destruct(kth_cashtoken_nft_spec_mut_t self);


// ---------------------------------------------------------------------------
// nft_mint_request
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_mint_request_mut_t kth_wallet_cashtoken_nft_mint_request_construct(
    kth_payment_address_const_t destination,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_token_capability_t capability,
    uint64_t satoshis);

KTH_EXPORT
void kth_wallet_cashtoken_nft_mint_request_destruct(
    kth_cashtoken_nft_mint_request_mut_t self);

// --- list ---

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_mint_request_list_mut_t kth_wallet_cashtoken_nft_mint_request_list_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_nft_mint_request_list_push_back(
    kth_cashtoken_nft_mint_request_list_mut_t list,
    kth_cashtoken_nft_mint_request_const_t elem);

KTH_EXPORT
void kth_wallet_cashtoken_nft_mint_request_list_destruct(
    kth_cashtoken_nft_mint_request_list_mut_t list);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_nft_mint_request_list_count(
    kth_cashtoken_nft_mint_request_list_const_t list);

KTH_EXPORT
kth_cashtoken_nft_mint_request_const_t kth_wallet_cashtoken_nft_mint_request_list_nth(
    kth_cashtoken_nft_mint_request_list_const_t list, kth_size_t index);


// ---------------------------------------------------------------------------
// nft_collection_item
// ---------------------------------------------------------------------------

/**
 * @param destination Optional — pass NULL to default to
 *                    `nft_collection_params::creator_address`.
 */
KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_collection_item_mut_t kth_wallet_cashtoken_nft_collection_item_construct(
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_payment_address_const_t destination);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_item_destruct(
    kth_cashtoken_nft_collection_item_mut_t self);

// --- list ---

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_collection_item_list_mut_t kth_wallet_cashtoken_nft_collection_item_list_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_item_list_push_back(
    kth_cashtoken_nft_collection_item_list_mut_t list,
    kth_cashtoken_nft_collection_item_const_t elem);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_item_list_destruct(
    kth_cashtoken_nft_collection_item_list_mut_t list);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_nft_collection_item_list_count(
    kth_cashtoken_nft_collection_item_list_const_t list);

/** @return Borrowed `kth_cashtoken_nft_collection_item_const_t` view into `list`. Do not destruct. */
KTH_EXPORT
kth_cashtoken_nft_collection_item_const_t kth_wallet_cashtoken_nft_collection_item_list_nth(
    kth_cashtoken_nft_collection_item_list_const_t list, kth_size_t index);


// ---------------------------------------------------------------------------
// prepare_genesis_params / prepare_genesis_result / prepare_genesis_utxo
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_prepare_genesis_params_mut_t kth_wallet_cashtoken_prepare_genesis_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_params_destruct(
    kth_cashtoken_prepare_genesis_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_params_set_utxo(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_utxo_const_t utxo);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_params_set_destination(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_payment_address_const_t destination);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_params_set_satoshis(
    kth_cashtoken_prepare_genesis_params_mut_t self, uint64_t satoshis);

/** @param change_address Optional — pass NULL for "no explicit change address". */
KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_params_set_change_address(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_payment_address_const_t change_address);

/** @param[out] out Must point to a null `kth_cashtoken_prepare_genesis_result_mut_t` slot. */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_prepare_genesis_utxo(
    kth_cashtoken_prepare_genesis_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_prepare_genesis_result_mut_t* out);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_result_destruct(
    kth_cashtoken_prepare_genesis_result_mut_t self);

/** @return Borrowed `kth_transaction_const_t` view; do not destruct. */
KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_prepare_genesis_result_transaction(
    kth_cashtoken_prepare_genesis_result_const_t self);

/** @return Number of signing indices. */
KTH_EXPORT
kth_size_t kth_wallet_cashtoken_prepare_genesis_result_signing_indices_count(
    kth_cashtoken_prepare_genesis_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_prepare_genesis_result_signing_index_nth(
    kth_cashtoken_prepare_genesis_result_const_t self, kth_size_t index);


// ---------------------------------------------------------------------------
// token_genesis_params / token_genesis_result / create_token_genesis
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_token_genesis_params_mut_t kth_wallet_cashtoken_token_genesis_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_destruct(
    kth_cashtoken_token_genesis_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_genesis_utxo(
    kth_cashtoken_token_genesis_params_mut_t self, kth_utxo_const_t genesis_utxo);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_destination(
    kth_cashtoken_token_genesis_params_mut_t self, kth_payment_address_const_t destination);

/** Sets the optional FT supply. `has_value == 0` clears the optional. */
KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_ft_amount(
    kth_cashtoken_token_genesis_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount);

/** @param nft Optional — pass NULL to clear the NFT spec. */
KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_nft(
    kth_cashtoken_token_genesis_params_mut_t self, kth_cashtoken_nft_spec_const_t nft);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_satoshis(
    kth_cashtoken_token_genesis_params_mut_t self, uint64_t satoshis);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_fee_utxos(
    kth_cashtoken_token_genesis_params_mut_t self, kth_utxo_list_const_t fee_utxos);

/** @param change_address Optional — pass NULL for no explicit change address. */
KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_change_address(
    kth_cashtoken_token_genesis_params_mut_t self, kth_payment_address_const_t change_address);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_params_set_script_flags(
    kth_cashtoken_token_genesis_params_mut_t self, uint64_t script_flags);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_genesis(
    kth_cashtoken_token_genesis_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out);

KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_result_destruct(
    kth_cashtoken_token_genesis_result_mut_t self);

KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_token_genesis_result_transaction(
    kth_cashtoken_token_genesis_result_const_t self);

/** Writes the 32-byte category id into `out_hash`. */
KTH_EXPORT
void kth_wallet_cashtoken_token_genesis_result_category_id(
    kth_cashtoken_token_genesis_result_const_t self, uint8_t* out_hash);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_token_genesis_result_signing_indices_count(
    kth_cashtoken_token_genesis_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_token_genesis_result_signing_index_nth(
    kth_cashtoken_token_genesis_result_const_t self, kth_size_t index);


// ---------------------------------------------------------------------------
// token_mint_params / token_mint_result / create_token_mint
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_token_mint_params_mut_t kth_wallet_cashtoken_token_mint_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_destruct(
    kth_cashtoken_token_mint_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_minting_utxo(
    kth_cashtoken_token_mint_params_mut_t self, kth_utxo_const_t minting_utxo);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_nfts(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_cashtoken_nft_mint_request_list_const_t nfts);

/**
 * Sets the optional preserved-minting commitment. `commitment == NULL`
 * clears the optional.
 */
KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_new_minting_commitment(
    kth_cashtoken_token_mint_params_mut_t self,
    uint8_t const* commitment, kth_size_t commitment_n);

/** Required — pass the address explicitly; no fallback to change_address. */
KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_minting_destination(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_payment_address_const_t minting_destination);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_fee_utxos(
    kth_cashtoken_token_mint_params_mut_t self, kth_utxo_list_const_t fee_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_change_address(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_payment_address_const_t change_address);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_params_set_script_flags(
    kth_cashtoken_token_mint_params_mut_t self, uint64_t script_flags);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_mint(
    kth_cashtoken_token_mint_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_mint_result_mut_t* out);

KTH_EXPORT
void kth_wallet_cashtoken_token_mint_result_destruct(
    kth_cashtoken_token_mint_result_mut_t self);

KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_token_mint_result_transaction(
    kth_cashtoken_token_mint_result_const_t self);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_token_mint_result_signing_indices_count(
    kth_cashtoken_token_mint_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_token_mint_result_signing_index_nth(
    kth_cashtoken_token_mint_result_const_t self, kth_size_t index);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_token_mint_result_minted_output_indices_count(
    kth_cashtoken_token_mint_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_token_mint_result_minted_output_index_nth(
    kth_cashtoken_token_mint_result_const_t self, kth_size_t index);


// ---------------------------------------------------------------------------
// token_transfer_params / create_token_transfer
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_token_transfer_params_mut_t kth_wallet_cashtoken_token_transfer_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_destruct(
    kth_cashtoken_token_transfer_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_token_utxos(
    kth_cashtoken_token_transfer_params_mut_t self, kth_utxo_list_const_t token_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_destination(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t destination);

/** `has_value == 0` clears the optional. */
KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_ft_amount(
    kth_cashtoken_token_transfer_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount);

/** @param nft Optional — pass NULL to clear. */
KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_nft(
    kth_cashtoken_token_transfer_params_mut_t self, kth_cashtoken_nft_spec_const_t nft);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_fee_utxos(
    kth_cashtoken_token_transfer_params_mut_t self, kth_utxo_list_const_t fee_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_token_change_address(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t addr);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_bch_change_address(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t addr);

KTH_EXPORT
void kth_wallet_cashtoken_token_transfer_params_set_satoshis(
    kth_cashtoken_token_transfer_params_mut_t self, uint64_t satoshis);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_transfer(
    kth_cashtoken_token_transfer_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out);


// ---------------------------------------------------------------------------
// token_burn_params / create_token_burn
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_token_burn_params_mut_t kth_wallet_cashtoken_token_burn_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_destruct(
    kth_cashtoken_token_burn_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_token_utxo(
    kth_cashtoken_token_burn_params_mut_t self, kth_utxo_const_t token_utxo);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_burn_ft_amount(
    kth_cashtoken_token_burn_params_mut_t self, kth_bool_t has_value, uint64_t burn_ft_amount);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_burn_nft(
    kth_cashtoken_token_burn_params_mut_t self, kth_bool_t burn_nft);

/** @param message Optional UTF-8 null-terminated string; NULL clears. */
KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_message(
    kth_cashtoken_token_burn_params_mut_t self, char const* message);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_destination(
    kth_cashtoken_token_burn_params_mut_t self, kth_payment_address_const_t destination);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_fee_utxos(
    kth_cashtoken_token_burn_params_mut_t self, kth_utxo_list_const_t fee_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_change_address(
    kth_cashtoken_token_burn_params_mut_t self, kth_payment_address_const_t change_address);

KTH_EXPORT
void kth_wallet_cashtoken_token_burn_params_set_satoshis(
    kth_cashtoken_token_burn_params_mut_t self, uint64_t satoshis);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_burn(
    kth_cashtoken_token_burn_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out);


// ---------------------------------------------------------------------------
// token_tx_result — shared between transfer and burn
// ---------------------------------------------------------------------------

KTH_EXPORT
void kth_wallet_cashtoken_token_tx_result_destruct(
    kth_cashtoken_token_tx_result_mut_t self);

KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_token_tx_result_transaction(
    kth_cashtoken_token_tx_result_const_t self);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_token_tx_result_signing_indices_count(
    kth_cashtoken_token_tx_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_token_tx_result_signing_index_nth(
    kth_cashtoken_token_tx_result_const_t self, kth_size_t index);


// ---------------------------------------------------------------------------
// ft_params / create_ft — high-level fungible token genesis
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_ft_params_mut_t kth_wallet_cashtoken_ft_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_destruct(
    kth_cashtoken_ft_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_genesis_utxo(
    kth_cashtoken_ft_params_mut_t self, kth_utxo_const_t genesis_utxo);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_destination(
    kth_cashtoken_ft_params_mut_t self, kth_payment_address_const_t destination);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_total_supply(
    kth_cashtoken_ft_params_mut_t self, uint64_t total_supply);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_with_minting_nft(
    kth_cashtoken_ft_params_mut_t self, kth_bool_t with_minting_nft);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_fee_utxos(
    kth_cashtoken_ft_params_mut_t self, kth_utxo_list_const_t fee_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_change_address(
    kth_cashtoken_ft_params_mut_t self, kth_payment_address_const_t change_address);

KTH_EXPORT
void kth_wallet_cashtoken_ft_params_set_script_flags(
    kth_cashtoken_ft_params_mut_t self, uint64_t script_flags);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_ft(
    kth_cashtoken_ft_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out);


// ---------------------------------------------------------------------------
// nft_collection_params / nft_collection_result / create_nft_collection
// ---------------------------------------------------------------------------

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_collection_params_mut_t kth_wallet_cashtoken_nft_collection_params_construct_default(void);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_destruct(
    kth_cashtoken_nft_collection_params_mut_t self);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_genesis_utxo(
    kth_cashtoken_nft_collection_params_mut_t self, kth_utxo_const_t genesis_utxo);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_nfts(
    kth_cashtoken_nft_collection_params_mut_t self,
    kth_cashtoken_nft_collection_item_list_const_t nfts);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_creator_address(
    kth_cashtoken_nft_collection_params_mut_t self, kth_payment_address_const_t creator_address);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_keep_minting_token(
    kth_cashtoken_nft_collection_params_mut_t self, kth_bool_t keep_minting_token);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_ft_amount(
    kth_cashtoken_nft_collection_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_fee_utxos(
    kth_cashtoken_nft_collection_params_mut_t self, kth_utxo_list_const_t fee_utxos);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_change_address(
    kth_cashtoken_nft_collection_params_mut_t self, kth_payment_address_const_t change_address);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_batch_size(
    kth_cashtoken_nft_collection_params_mut_t self, kth_size_t batch_size);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_params_set_script_flags(
    kth_cashtoken_nft_collection_params_mut_t self, uint64_t script_flags);

KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_nft_collection(
    kth_cashtoken_nft_collection_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_nft_collection_result_mut_t* out);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_result_destruct(
    kth_cashtoken_nft_collection_result_mut_t self);

KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_nft_collection_result_genesis_transaction(
    kth_cashtoken_nft_collection_result_const_t self);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_nft_collection_result_genesis_signing_indices_count(
    kth_cashtoken_nft_collection_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_nft_collection_result_genesis_signing_index_nth(
    kth_cashtoken_nft_collection_result_const_t self, kth_size_t index);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_result_category_id(
    kth_cashtoken_nft_collection_result_const_t self, uint8_t* out_hash);

KTH_EXPORT
kth_bool_t kth_wallet_cashtoken_nft_collection_result_final_burn(
    kth_cashtoken_nft_collection_result_const_t self);

/** Number of mint batches. */
KTH_EXPORT
kth_size_t kth_wallet_cashtoken_nft_collection_result_batches_count(
    kth_cashtoken_nft_collection_result_const_t self);

/**
 * Returns a borrowed view of a batch's mint-requests list. Caller must
 * NOT destruct this list — it's owned by the parent result handle and
 * invalidated when the result is destructed.
 */
KTH_EXPORT
kth_cashtoken_nft_mint_request_list_const_t kth_wallet_cashtoken_nft_collection_result_batch_mint_requests(
    kth_cashtoken_nft_collection_result_const_t self, kth_size_t batch_index);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_CASHTOKEN_MINTING_H_
