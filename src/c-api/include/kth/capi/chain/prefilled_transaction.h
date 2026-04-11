// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_PREFILLED_TRANSACTION_H_
#define KTH_CAPI_CHAIN_PREFILLED_TRANSACTION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_prefilled_transaction_mut_t`. Caller must release with `kth_chain_prefilled_transaction_destruct`. */
KTH_EXPORT KTH_OWNED
kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_construct_default(void);

/** @param[out] out Must point to a null `kth_prefilled_transaction_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_prefilled_transaction_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_prefilled_transaction_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_prefilled_transaction_mut_t* out);

/**
 * @return Owned `kth_prefilled_transaction_mut_t`. Caller must release with `kth_chain_prefilled_transaction_destruct`.
 * @param tx Borrowed input. Copied by value into the resulting object; ownership of `tx` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_construct(uint64_t index, kth_transaction_const_t tx);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_prefilled_transaction_destruct(kth_prefilled_transaction_mut_t self);


// Copy

/** @return Owned `kth_prefilled_transaction_mut_t`. Caller must release with `kth_chain_prefilled_transaction_destruct`. */
KTH_EXPORT KTH_OWNED
kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_copy(kth_prefilled_transaction_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_prefilled_transaction_equals(kth_prefilled_transaction_const_t self, kth_prefilled_transaction_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_prefilled_transaction_to_data(kth_prefilled_transaction_const_t self, uint32_t version, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_prefilled_transaction_serialized_size(kth_prefilled_transaction_const_t self, uint32_t version);


// Getters

KTH_EXPORT
uint64_t kth_chain_prefilled_transaction_index(kth_prefilled_transaction_const_t self);

/** @return Borrowed `kth_transaction_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_transaction_const_t kth_chain_prefilled_transaction_transaction(kth_prefilled_transaction_const_t self);


// Setters

KTH_EXPORT
void kth_chain_prefilled_transaction_set_index(kth_prefilled_transaction_mut_t self, uint64_t value);

/** @param tx Borrowed input. Copied by value into the resulting object; ownership of `tx` stays with the caller. */
KTH_EXPORT
void kth_chain_prefilled_transaction_set_transaction(kth_prefilled_transaction_mut_t self, kth_transaction_const_t tx);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_prefilled_transaction_is_valid(kth_prefilled_transaction_const_t self);


// Operations

KTH_EXPORT
void kth_chain_prefilled_transaction_reset(kth_prefilled_transaction_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_PREFILLED_TRANSACTION_H_
