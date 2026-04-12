// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_TRANSACTION_H_
#define KTH_CAPI_CHAIN_TRANSACTION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_transaction_mut_t`. Caller must release with `kth_chain_transaction_destruct`. */
KTH_EXPORT KTH_OWNED
kth_transaction_mut_t kth_chain_transaction_construct_default(void);

/** @param[out] out Must point to a null `kth_transaction_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_transaction_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_transaction_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_transaction_mut_t* out);

/**
 * @return Owned `kth_transaction_mut_t`. Caller must release with `kth_chain_transaction_destruct`.
 * @param inputs Borrowed input. Copied by value into the resulting object; ownership of `inputs` stays with the caller.
 * @param outputs Borrowed input. Copied by value into the resulting object; ownership of `outputs` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_transaction_mut_t kth_chain_transaction_construct_from_version_locktime_inputs_outputs(uint32_t version, uint32_t locktime, kth_input_list_const_t inputs, kth_output_list_const_t outputs);

/**
 * @return Owned `kth_transaction_mut_t`. Caller must release with `kth_chain_transaction_destruct`.
 * @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_transaction_mut_t kth_chain_transaction_construct_from_transaction_hash(kth_transaction_const_t x, kth_hash_t hash);

/**
 * @return Owned `kth_transaction_mut_t`. Caller must release with `kth_chain_transaction_destruct`.
 * @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller.
 * @warning `hash` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_transaction_mut_t kth_chain_transaction_construct_from_transaction_hash_unsafe(kth_transaction_const_t x, uint8_t const* hash);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_transaction_destruct(kth_transaction_mut_t self);


// Copy

/** @return Owned `kth_transaction_mut_t`. Caller must release with `kth_chain_transaction_destruct`. */
KTH_EXPORT KTH_OWNED
kth_transaction_mut_t kth_chain_transaction_copy(kth_transaction_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_transaction_equals(kth_transaction_const_t self, kth_transaction_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_transaction_to_data(kth_transaction_const_t self, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_transaction_serialized_size(kth_transaction_const_t self, kth_bool_t wire);


// Getters

KTH_EXPORT
kth_hash_t kth_chain_transaction_outputs_hash(kth_transaction_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_transaction_inpoints_hash(kth_transaction_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_transaction_sequences_hash(kth_transaction_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_transaction_utxos_hash(kth_transaction_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_transaction_hash(kth_transaction_const_t self);

KTH_EXPORT
uint64_t kth_chain_transaction_fees(kth_transaction_const_t self);

KTH_EXPORT
uint64_t kth_chain_transaction_total_input_value(kth_transaction_const_t self);

KTH_EXPORT
uint64_t kth_chain_transaction_total_output_value(kth_transaction_const_t self);

KTH_EXPORT
kth_size_t kth_chain_transaction_signature_operations_simple(kth_transaction_const_t self);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect_simple(kth_transaction_const_t self);

KTH_EXPORT
uint32_t kth_chain_transaction_version(kth_transaction_const_t self);

KTH_EXPORT
uint32_t kth_chain_transaction_locktime(kth_transaction_const_t self);

/** @return Borrowed `kth_input_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_input_list_const_t kth_chain_transaction_inputs(kth_transaction_const_t self);

/** @return Borrowed `kth_output_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_output_list_const_t kth_chain_transaction_outputs(kth_transaction_const_t self);

/** @return Owned `kth_point_list_mut_t`. Caller must release with `kth_chain_point_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_point_list_mut_t kth_chain_transaction_previous_outputs(kth_transaction_const_t self);

/** @return Owned `kth_point_list_mut_t`. Caller must release with `kth_chain_point_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_point_list_mut_t kth_chain_transaction_missing_previous_outputs(kth_transaction_const_t self);

/** @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_transaction_missing_previous_transactions(kth_transaction_const_t self);


// Setters

KTH_EXPORT
void kth_chain_transaction_set_version(kth_transaction_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_transaction_set_locktime(kth_transaction_mut_t self, uint32_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_transaction_set_inputs(kth_transaction_mut_t self, kth_input_list_const_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_transaction_set_outputs(kth_transaction_mut_t self, kth_output_list_const_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_overspent(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_valid(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_coinbase(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_null_non_coinbase(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_oversized_coinbase(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_mature(kth_transaction_const_t self, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_internal_double_spend(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_double_spend(kth_transaction_const_t self, kth_bool_t include_unconfirmed);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_dusty(kth_transaction_const_t self, uint64_t minimum_output_value);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_missing_previous_outputs(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_final(kth_transaction_const_t self, kth_size_t block_height, uint32_t block_time);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_locked(kth_transaction_const_t self, kth_size_t block_height, uint32_t median_time_past);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_locktime_conflict(kth_transaction_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_standard(kth_transaction_const_t self);


// Operations

KTH_EXPORT
void kth_chain_transaction_recompute_hash(kth_transaction_mut_t self);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_check(kth_transaction_const_t self, kth_size_t max_block_size, kth_bool_t transaction_pool, kth_bool_t retarget);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_accept(kth_transaction_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transaction_pool);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect(kth_transaction_const_t self, kth_chain_state_const_t state);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect_input(kth_transaction_const_t self, kth_chain_state_const_t state, kth_size_t input_index);

KTH_EXPORT
void kth_chain_transaction_reset(kth_transaction_mut_t self);

KTH_EXPORT
kth_size_t kth_chain_transaction_signature_operations(kth_transaction_const_t self, kth_bool_t bip16, kth_bool_t bip141);

KTH_EXPORT
kth_size_t kth_chain_transaction_min_tx_size(kth_transaction_const_t self, kth_script_flags_t flags);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_TRANSACTION_H_
