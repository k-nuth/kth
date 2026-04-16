// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_BLOCK_H_
#define KTH_CAPI_CHAIN_BLOCK_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_block_mut_t`. Caller must release with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_construct_default(void);

/** @param[out] out Must point to a null `kth_block_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_block_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_block_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_block_mut_t* out);

/**
 * @return Owned `kth_block_mut_t`. Caller must release with `kth_chain_block_destruct`.
 * @param header Borrowed input. Copied by value into the resulting object; ownership of `header` stays with the caller.
 * @param transactions Borrowed input. Copied by value into the resulting object; ownership of `transactions` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_construct(kth_header_const_t header, kth_transaction_list_const_t transactions);


// Static factories

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_mainnet(void);

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_testnet(void);

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_regtest(void);

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_testnet4(void);

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_scalenet(void);

/** @return Owned `kth_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_genesis_chipnet(void);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_block_destruct(kth_block_mut_t self);


// Copy

/** @return Owned `kth_block_mut_t`. Caller must release with `kth_chain_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_mut_t kth_chain_block_copy(kth_block_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_block_equals(kth_block_const_t self, kth_block_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_block_to_data_simple(kth_block_const_t self, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_block_serialized_size(kth_block_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_block_to_data(kth_block_const_t self, kth_size_t serialized_size, kth_size_t* out_size);


// Getters

KTH_EXPORT
kth_size_t kth_chain_block_signature_operations_simple(kth_block_const_t self);

KTH_EXPORT
kth_error_code_t kth_chain_block_check(kth_block_const_t self);

KTH_EXPORT
kth_error_code_t kth_chain_block_connect(kth_block_const_t self);

/** @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_block_to_hashes(kth_block_const_t self);

/** @return Borrowed `kth_header_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_header_const_t kth_chain_block_header(kth_block_const_t self);

/** @return Borrowed `kth_transaction_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_transaction_list_const_t kth_chain_block_transactions(kth_block_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_block_hash(kth_block_const_t self);

KTH_EXPORT
uint64_t kth_chain_block_fees(kth_block_const_t self);

KTH_EXPORT
uint64_t kth_chain_block_claim(kth_block_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_block_generate_merkle_root(kth_block_const_t self);

KTH_EXPORT
kth_error_code_t kth_chain_block_check_transactions(kth_block_const_t self);

KTH_EXPORT
kth_size_t kth_chain_block_non_coinbase_input_count(kth_block_const_t self);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_block_set_transactions(kth_block_mut_t self, kth_transaction_list_const_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_block_set_header(kth_block_mut_t self, kth_header_const_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_block_is_valid(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_extra_coinbases(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_final(kth_block_const_t self, kth_size_t height, uint32_t block_time);

KTH_EXPORT
kth_bool_t kth_chain_block_is_distinct_transaction_set(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_valid_coinbase_claim(kth_block_const_t self, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_block_is_valid_coinbase_script(kth_block_const_t self, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_block_is_forward_reference(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_canonical_ordered(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_internal_double_spend(kth_block_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_block_is_valid_merkle_root(kth_block_const_t self);


// Operations

KTH_EXPORT
kth_size_t kth_chain_block_total_inputs(kth_block_const_t self, kth_bool_t with_coinbase);

KTH_EXPORT
kth_error_code_t kth_chain_block_accept(kth_block_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_block_size_dynamic, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transactions);

KTH_EXPORT
uint64_t kth_chain_block_reward(kth_block_const_t self, kth_size_t height);

KTH_EXPORT
kth_size_t kth_chain_block_signature_operations(kth_block_const_t self, kth_bool_t bip16, kth_bool_t bip141);

KTH_EXPORT
kth_error_code_t kth_chain_block_accept_transactions(kth_block_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint);

KTH_EXPORT
kth_error_code_t kth_chain_block_connect_transactions(kth_block_const_t self, kth_chain_state_const_t state);

KTH_EXPORT
void kth_chain_block_reset(kth_block_mut_t self);


// Static utilities

KTH_EXPORT
kth_size_t kth_chain_block_locator_size(kth_size_t top);

/** @return Owned `kth_u64_list_mut_t`. Caller must release with `kth_core_u64_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_u64_list_mut_t kth_chain_block_locator_heights(kth_size_t top);

KTH_EXPORT
uint64_t kth_chain_block_subsidy(kth_size_t height, kth_bool_t retarget);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_BLOCK_H_
