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
// Opaque handles
// ---------------------------------------------------------------------------
//
// This module deliberately does NOT expose the domain `*_params` structs
// via handles. Those structs contain required `payment_address` members
// which are not default-constructible under the current domain shape;
// exposing them would either need sentinel state (dropped project-wide)
// or a parallel C-API-side "holder" struct that would drift over time.
//
// Instead, each transaction-builder is a single `create_*` function that
// takes every field as an argument. Optional fields use the same NULL /
// has_value conventions used elsewhere in the C-API. Only the value
// types that are useful to marshal individually (nft_spec, the list
// element types, and the result types) keep a handle.

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

typedef void*       kth_cashtoken_prepare_genesis_result_mut_t;
typedef void const* kth_cashtoken_prepare_genesis_result_const_t;

typedef void*       kth_cashtoken_token_genesis_result_mut_t;
typedef void const* kth_cashtoken_token_genesis_result_const_t;

typedef void*       kth_cashtoken_token_mint_result_mut_t;
typedef void const* kth_cashtoken_token_mint_result_const_t;

typedef void*       kth_cashtoken_token_tx_result_mut_t;
typedef void const* kth_cashtoken_token_tx_result_const_t;

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
// nft_mint_request (single element + list)
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

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_mint_request_list_mut_t
kth_wallet_cashtoken_nft_mint_request_list_construct_default(void);

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
// nft_collection_item (single element + list)
// ---------------------------------------------------------------------------

/**
 * @param destination Optional — pass NULL to default to
 *                    `create_nft_collection`'s `creator_address`.
 */
KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_collection_item_mut_t kth_wallet_cashtoken_nft_collection_item_construct(
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_payment_address_const_t destination);

KTH_EXPORT
void kth_wallet_cashtoken_nft_collection_item_destruct(
    kth_cashtoken_nft_collection_item_mut_t self);

KTH_EXPORT KTH_OWNED
kth_cashtoken_nft_collection_item_list_mut_t
kth_wallet_cashtoken_nft_collection_item_list_construct_default(void);

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

/** @return Borrowed `kth_cashtoken_nft_collection_item_const_t`. Do not destruct. */
KTH_EXPORT
kth_cashtoken_nft_collection_item_const_t kth_wallet_cashtoken_nft_collection_item_list_nth(
    kth_cashtoken_nft_collection_item_list_const_t list, kth_size_t index);


// ---------------------------------------------------------------------------
// prepare_genesis_utxo
// ---------------------------------------------------------------------------

/**
 * Build the "carrier" transaction that produces a UTXO at output 0
 * suitable for a subsequent token genesis input.
 *
 * @param utxo             Required. A spendable UTXO to fund the carrier tx.
 * @param destination      Required. Recipient of the new output 0.
 * @param satoshis         Value carried by the new output 0.
 * @param change_address   Optional — pass NULL for "no explicit change address".
 * @param[out] out         Must point to a null slot. On success, populated
 *                         with an owned handle. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_prepare_genesis_utxo(
    kth_utxo_const_t utxo,
    kth_payment_address_const_t destination,
    uint64_t satoshis,
    kth_payment_address_const_t change_address,
    KTH_OUT_OWNED kth_cashtoken_prepare_genesis_result_mut_t* out);

KTH_EXPORT
void kth_wallet_cashtoken_prepare_genesis_result_destruct(
    kth_cashtoken_prepare_genesis_result_mut_t self);

/** @return Borrowed `kth_transaction_const_t` view. Do not destruct. */
KTH_EXPORT
kth_transaction_const_t kth_wallet_cashtoken_prepare_genesis_result_transaction(
    kth_cashtoken_prepare_genesis_result_const_t self);

KTH_EXPORT
kth_size_t kth_wallet_cashtoken_prepare_genesis_result_signing_indices_count(
    kth_cashtoken_prepare_genesis_result_const_t self);

KTH_EXPORT
uint32_t kth_wallet_cashtoken_prepare_genesis_result_signing_index_nth(
    kth_cashtoken_prepare_genesis_result_const_t self, kth_size_t index);


// ---------------------------------------------------------------------------
// create_token_genesis
// ---------------------------------------------------------------------------

/**
 * @param genesis_utxo     Required. Must have `outpoint.index() == 0`.
 * @param destination      Required.
 * @param has_ft_amount    Non-zero if `ft_amount` should be set.
 * @param ft_amount        Read only when `has_ft_amount != 0`.
 * @param nft              Optional — pass NULL for no NFT.
 * @param satoshis         BCH satoshis carried by the token output.
 * @param fee_utxos        Optional — pass NULL for "no fee UTXOs".
 * @param change_address   Optional — pass NULL for no explicit change address.
 * @param script_flags     Active script flags for commitment-size validation.
 * @param[out] out         Must point to a null slot. Populated on success;
 *                         untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_genesis(
    kth_utxo_const_t genesis_utxo,
    kth_payment_address_const_t destination,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_cashtoken_nft_spec_const_t nft,
    uint64_t satoshis,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
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
// create_token_mint
// ---------------------------------------------------------------------------

/**
 * @param minting_utxo             Required. Must hold an NFT with capability == minting.
 * @param nfts                     Required. NFTs to mint.
 * @param new_minting_commitment   Optional — pass NULL for no commitment replacement.
 * @param new_minting_commitment_n Ignored when `new_minting_commitment == NULL`.
 * @param minting_destination      Required by the domain builder. Passing NULL
 *                                 surfaces as `error::illegal_value`.
 * @param fee_utxos                Optional — pass NULL for no fee UTXOs.
 * @param change_address           Optional — pass NULL for no explicit change address.
 * @param script_flags             Active script flags for commitment-size validation.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_mint(
    kth_utxo_const_t minting_utxo,
    kth_cashtoken_nft_mint_request_list_const_t nfts,
    uint8_t const* new_minting_commitment,
    kth_size_t new_minting_commitment_n,
    kth_payment_address_const_t minting_destination,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
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
// create_token_transfer
// ---------------------------------------------------------------------------

/**
 * @param token_utxos                Optional — pass NULL for no token UTXOs.
 * @param destination                Required.
 * @param has_ft_amount              Non-zero if `ft_amount` should be set.
 * @param nft                        Optional — pass NULL for no NFT spec.
 * @param fee_utxos                  Optional — pass NULL for no fee UTXOs.
 * @param token_change_address       Optional — pass NULL.
 * @param bch_change_address         Optional — pass NULL.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_transfer(
    kth_utxo_list_const_t token_utxos,
    kth_payment_address_const_t destination,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_cashtoken_nft_spec_const_t nft,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t token_change_address,
    kth_payment_address_const_t bch_change_address,
    uint64_t satoshis,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out);


// ---------------------------------------------------------------------------
// create_token_burn
// ---------------------------------------------------------------------------

/**
 * @param token_utxo         Required.
 * @param has_burn_ft_amount Non-zero if `burn_ft_amount` should be set.
 * @param message            Optional UTF-8 null-terminated string; NULL clears.
 * @param destination        Required.
 * @param fee_utxos          Optional — pass NULL for no fee UTXOs.
 * @param change_address     Optional — pass NULL.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_token_burn(
    kth_utxo_const_t token_utxo,
    kth_bool_t has_burn_ft_amount, uint64_t burn_ft_amount,
    kth_bool_t burn_nft,
    char const* message,
    kth_payment_address_const_t destination,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t satoshis,
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
// create_ft — high-level fungible token genesis
// ---------------------------------------------------------------------------

/**
 * @param genesis_utxo     Required.
 * @param destination      Required.
 * @param fee_utxos        Optional — pass NULL for no fee UTXOs.
 * @param change_address   Optional — pass NULL.
 * @param script_flags     Only relevant when `with_minting_nft != 0`.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_ft(
    kth_utxo_const_t genesis_utxo,
    kth_payment_address_const_t destination,
    uint64_t total_supply,
    kth_bool_t with_minting_nft,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out);


// ---------------------------------------------------------------------------
// create_nft_collection
// ---------------------------------------------------------------------------

/**
 * @param genesis_utxo         Required.
 * @param nfts                 Required. Collection items.
 * @param creator_address      Required. Default destination for items without one.
 * @param has_ft_amount        Non-zero if `ft_amount` should be set.
 * @param fee_utxos            Optional — pass NULL for no fee UTXOs.
 * @param change_address       Optional — pass NULL.
 * @param batch_size           NFTs per mint TX; must be > 0.
 * @param script_flags         Active script flags for commitment-size validation.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_cashtoken_create_nft_collection(
    kth_utxo_const_t genesis_utxo,
    kth_cashtoken_nft_collection_item_list_const_t nfts,
    kth_payment_address_const_t creator_address,
    kth_bool_t keep_minting_token,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    kth_size_t batch_size,
    uint64_t script_flags,
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
 * @return Borrowed view of a batch's mint-requests list. Caller must NOT
 *         destruct — owned by the parent result handle.
 */
KTH_EXPORT
kth_cashtoken_nft_mint_request_list_const_t kth_wallet_cashtoken_nft_collection_result_batch_mint_requests(
    kth_cashtoken_nft_collection_result_const_t self, kth_size_t batch_index);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_CASHTOKEN_MINTING_H_
