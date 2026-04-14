// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_GET_HEADERS_H_
#define KTH_CAPI_CHAIN_GET_HEADERS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_get_headers_mut_t`. Caller must release with `kth_chain_get_headers_destruct`. */
KTH_EXPORT KTH_OWNED
kth_get_headers_mut_t kth_chain_get_headers_construct_default(void);

/** @param[out] out Must point to a null `kth_get_headers_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_get_headers_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_get_headers_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_get_headers_mut_t* out);

/**
 * @return Owned `kth_get_headers_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_get_headers_destruct`.
 * @param start Borrowed input. Copied by value into the resulting object; ownership of `start` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_get_headers_mut_t kth_chain_get_headers_construct(kth_hash_list_const_t start, kth_hash_t stop);

/**
 * @return Owned `kth_get_headers_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_get_headers_destruct`.
 * @param start Borrowed input. Copied by value into the resulting object; ownership of `start` stays with the caller.
 * @warning `stop` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_get_headers_mut_t kth_chain_get_headers_construct_unsafe(kth_hash_list_const_t start, uint8_t const* stop);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_get_headers_destruct(kth_get_headers_mut_t self);


// Copy

/** @return Owned `kth_get_headers_mut_t`. Caller must release with `kth_chain_get_headers_destruct`. */
KTH_EXPORT KTH_OWNED
kth_get_headers_mut_t kth_chain_get_headers_copy(kth_get_headers_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_get_headers_equals(kth_get_headers_const_t self, kth_get_headers_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_get_headers_to_data(kth_get_headers_const_t self, uint32_t version, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_get_headers_serialized_size(kth_get_headers_const_t self, uint32_t version);


// Getters

/** @return Borrowed `kth_hash_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_hash_list_const_t kth_chain_get_headers_start_hashes(kth_get_headers_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_get_headers_stop_hash(kth_get_headers_const_t self);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_get_headers_set_start_hashes(kth_get_headers_mut_t self, kth_hash_list_const_t value);

KTH_EXPORT
void kth_chain_get_headers_set_stop_hash(kth_get_headers_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_get_headers_set_stop_hash_unsafe(kth_get_headers_mut_t self, uint8_t const* value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_get_headers_is_valid(kth_get_headers_const_t self);


// Operations

KTH_EXPORT
void kth_chain_get_headers_reset(kth_get_headers_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_GET_HEADERS_H_
