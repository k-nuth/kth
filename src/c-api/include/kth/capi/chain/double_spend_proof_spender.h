// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_SPENDER_H_
#define KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_SPENDER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @param[out] out Must point to a null `kth_double_spend_proof_spender_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_double_spend_proof_spender_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_double_spend_proof_spender_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_double_spend_proof_spender_mut_t* out);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_double_spend_proof_spender_destruct(kth_double_spend_proof_spender_mut_t self);


// Copy

/** @return Owned `kth_double_spend_proof_spender_mut_t`. Caller must release with `kth_chain_double_spend_proof_spender_destruct`. */
KTH_EXPORT KTH_OWNED
kth_double_spend_proof_spender_mut_t kth_chain_double_spend_proof_spender_copy(kth_double_spend_proof_spender_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_double_spend_proof_spender_equals(kth_double_spend_proof_spender_const_t self, kth_double_spend_proof_spender_const_t other);


// Serialization

KTH_EXPORT
kth_size_t kth_chain_double_spend_proof_spender_serialized_size(kth_double_spend_proof_spender_const_t self);


// Getters

KTH_EXPORT
uint32_t kth_chain_double_spend_proof_spender_version(kth_double_spend_proof_spender_const_t self);

KTH_EXPORT
uint32_t kth_chain_double_spend_proof_spender_out_sequence(kth_double_spend_proof_spender_const_t self);

KTH_EXPORT
uint32_t kth_chain_double_spend_proof_spender_locktime(kth_double_spend_proof_spender_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_double_spend_proof_spender_prev_outs_hash(kth_double_spend_proof_spender_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_double_spend_proof_spender_sequence_hash(kth_double_spend_proof_spender_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_double_spend_proof_spender_outputs_hash(kth_double_spend_proof_spender_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_double_spend_proof_spender_push_data(kth_double_spend_proof_spender_const_t self, kth_size_t* out_size);


// Setters

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_version(kth_double_spend_proof_spender_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_out_sequence(kth_double_spend_proof_spender_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_locktime(kth_double_spend_proof_spender_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_prev_outs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_prev_outs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_sequence_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_sequence_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_outputs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_outputs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_double_spend_proof_spender_set_push_data(kth_double_spend_proof_spender_mut_t self, uint8_t const* value, kth_size_t n);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_double_spend_proof_spender_is_valid(kth_double_spend_proof_spender_const_t self);


// Operations

KTH_EXPORT
void kth_chain_double_spend_proof_spender_reset(kth_double_spend_proof_spender_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_SPENDER_H_
