// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_UTXO_H_
#define KTH_CAPI_CHAIN_UTXO_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_utxo_mut_t`. Caller must release with `kth_chain_utxo_destruct`. */
KTH_EXPORT KTH_OWNED
kth_utxo_mut_t kth_chain_utxo_construct_default(void);

/**
 * @return Owned `kth_utxo_mut_t`. Caller must release with `kth_chain_utxo_destruct`.
 * @param point Borrowed input. Copied by value into the resulting object; ownership of `point` stays with the caller.
 * @param token_data Borrowed input. Copied by value into the resulting object; ownership of `token_data` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_utxo_mut_t kth_chain_utxo_construct(kth_output_point_const_t point, uint64_t amount, kth_token_data_const_t token_data);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_utxo_destruct(kth_utxo_mut_t self);


// Copy

/** @return Owned `kth_utxo_mut_t`. Caller must release with `kth_chain_utxo_destruct`. */
KTH_EXPORT KTH_OWNED
kth_utxo_mut_t kth_chain_utxo_copy(kth_utxo_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_utxo_equals(kth_utxo_const_t self, kth_utxo_const_t other);


// Getters

KTH_EXPORT
uint32_t kth_chain_utxo_height(kth_utxo_const_t self);

/** @return Borrowed `kth_output_point_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_output_point_const_t kth_chain_utxo_point(kth_utxo_const_t self);

KTH_EXPORT
uint64_t kth_chain_utxo_amount(kth_utxo_const_t self);

/** @return Borrowed `kth_token_data_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_token_data_const_t kth_chain_utxo_token_data(kth_utxo_const_t self);


// Setters

KTH_EXPORT
void kth_chain_utxo_set_height(kth_utxo_mut_t self, uint32_t height);

/** @param point Borrowed input. Copied by value into the resulting object; ownership of `point` stays with the caller. */
KTH_EXPORT
void kth_chain_utxo_set_point(kth_utxo_mut_t self, kth_output_point_const_t point);

KTH_EXPORT
void kth_chain_utxo_set_amount(kth_utxo_mut_t self, uint64_t amount);

/** @param token_data Borrowed input. Copied by value into the resulting object; ownership of `token_data` stays with the caller. */
KTH_EXPORT
void kth_chain_utxo_set_token_data(kth_utxo_mut_t self, kth_token_data_const_t token_data);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_UTXO_H_
