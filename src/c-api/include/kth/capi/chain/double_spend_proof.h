// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_H_
#define KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_double_spend_proof_mut_t`. Caller must release with `kth_chain_double_spend_proof_destruct`. */
KTH_EXPORT KTH_OWNED
kth_double_spend_proof_mut_t kth_chain_double_spend_proof_construct_default(void);

/** @param[out] out Must point to a null `kth_double_spend_proof_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_double_spend_proof_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_double_spend_proof_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_double_spend_proof_mut_t* out);

/**
 * @return Owned `kth_double_spend_proof_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_double_spend_proof_destruct`.
 * @param out_point Borrowed input. Copied by value into the resulting object; ownership of `out_point` stays with the caller.
 * @param spender1 Borrowed input. Copied by value into the resulting object; ownership of `spender1` stays with the caller.
 * @param spender2 Borrowed input. Copied by value into the resulting object; ownership of `spender2` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_double_spend_proof_mut_t kth_chain_double_spend_proof_construct(kth_output_point_const_t out_point, kth_double_spend_proof_spender_const_t spender1, kth_double_spend_proof_spender_const_t spender2);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_double_spend_proof_destruct(kth_double_spend_proof_mut_t self);


// Copy

/** @return Owned `kth_double_spend_proof_mut_t`. Caller must release with `kth_chain_double_spend_proof_destruct`. */
KTH_EXPORT KTH_OWNED
kth_double_spend_proof_mut_t kth_chain_double_spend_proof_copy(kth_double_spend_proof_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_double_spend_proof_equals(kth_double_spend_proof_const_t self, kth_double_spend_proof_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_double_spend_proof_to_data(kth_double_spend_proof_const_t self, kth_size_t version, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_double_spend_proof_serialized_size(kth_double_spend_proof_const_t self, kth_size_t version);


// Getters

/** @return Borrowed `kth_output_point_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_output_point_const_t kth_chain_double_spend_proof_out_point(kth_double_spend_proof_const_t self);

/** @return Borrowed `kth_double_spend_proof_spender_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_double_spend_proof_spender_const_t kth_chain_double_spend_proof_spender1(kth_double_spend_proof_const_t self);

/** @return Borrowed `kth_double_spend_proof_spender_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_double_spend_proof_spender_const_t kth_chain_double_spend_proof_spender2(kth_double_spend_proof_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_double_spend_proof_hash(kth_double_spend_proof_const_t self);


// Setters

/** @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller. */
KTH_EXPORT
void kth_chain_double_spend_proof_set_out_point(kth_double_spend_proof_mut_t self, kth_output_point_const_t x);

/** @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller. */
KTH_EXPORT
void kth_chain_double_spend_proof_set_spender1(kth_double_spend_proof_mut_t self, kth_double_spend_proof_spender_const_t x);

/** @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller. */
KTH_EXPORT
void kth_chain_double_spend_proof_set_spender2(kth_double_spend_proof_mut_t self, kth_double_spend_proof_spender_const_t x);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_double_spend_proof_is_valid(kth_double_spend_proof_const_t self);


// Operations

KTH_EXPORT
void kth_chain_double_spend_proof_reset(kth_double_spend_proof_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_DOUBLE_SPEND_PROOF_H_
