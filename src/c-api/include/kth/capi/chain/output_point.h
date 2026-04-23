// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_OUTPUT_POINT_H_
#define KTH_CAPI_CHAIN_OUTPUT_POINT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_construct_default(void);

/** @param[out] out Must point to a null `kth_output_point_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_output_point_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_output_point_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_output_point_mut_t* out);

/**
 * @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`.
 * @param hash Borrowed input; must be non-null. Copied into the resulting object; ownership of `hash` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_construct_from_hash_index(kth_hash_t const* hash, uint32_t index);

/**
 * @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`.
 * @warning `hash` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_construct_from_hash_index_unsafe(uint8_t const* hash, uint32_t index);

/**
 * @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`.
 * @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_construct_from_point(kth_point_const_t x);


// Static factories

/** @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_null(void);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_output_point_destruct(kth_output_point_mut_t self);


// Copy

/** @return Owned `kth_output_point_mut_t`. Caller must release with `kth_chain_output_point_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_point_mut_t kth_chain_output_point_copy(kth_output_point_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_output_point_equals(kth_output_point_const_t self, kth_output_point_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_output_point_to_data(kth_output_point_const_t self, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_output_point_serialized_size(kth_output_point_const_t self, kth_bool_t wire);


// Getters

KTH_EXPORT
kth_hash_t kth_chain_output_point_hash(kth_output_point_const_t self);

KTH_EXPORT
uint32_t kth_chain_output_point_index(kth_output_point_const_t self);

KTH_EXPORT
uint64_t kth_chain_output_point_checksum(kth_output_point_const_t self);


// Setters

/** @param value Borrowed input; must be non-null. Copied into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_output_point_set_hash(kth_output_point_mut_t self, kth_hash_t const* value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`. */
KTH_EXPORT
void kth_chain_output_point_set_hash_unsafe(kth_output_point_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_output_point_set_index(kth_output_point_mut_t self, uint32_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_output_point_is_mature(kth_output_point_const_t self, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_output_point_is_valid(kth_output_point_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_output_point_is_null(kth_output_point_const_t self);


// Operations

KTH_EXPORT
void kth_chain_output_point_reset(kth_output_point_mut_t self);


// Static utilities

KTH_EXPORT
kth_size_t kth_chain_output_point_satoshi_fixed_size(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_OUTPUT_POINT_H_
