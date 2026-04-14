// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MERKLE_BLOCK_H_
#define KTH_CAPI_CHAIN_MERKLE_BLOCK_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_merkle_block_mut_t`. Caller must release with `kth_chain_merkle_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_merkle_block_mut_t kth_chain_merkle_block_construct_default(void);

/** @param[out] out Must point to a null `kth_merkle_block_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_merkle_block_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_merkle_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_merkle_block_mut_t* out);

/**
 * @return Owned `kth_merkle_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_merkle_block_destruct`.
 * @param header Borrowed input. Copied by value into the resulting object; ownership of `header` stays with the caller.
 * @param hashes Borrowed input. Copied by value into the resulting object; ownership of `hashes` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_merkle_block_mut_t kth_chain_merkle_block_construct_from_header_total_transactions_hashes_flags(kth_header_const_t header, kth_size_t total_transactions, kth_hash_list_const_t hashes, uint8_t const* flags, kth_size_t n);

/**
 * @return Owned `kth_merkle_block_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_merkle_block_destruct`.
 * @param block Borrowed input. Copied by value into the resulting object; ownership of `block` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_merkle_block_mut_t kth_chain_merkle_block_construct_from_block(kth_block_const_t block);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_merkle_block_destruct(kth_merkle_block_mut_t self);


// Copy

/** @return Owned `kth_merkle_block_mut_t`. Caller must release with `kth_chain_merkle_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_merkle_block_mut_t kth_chain_merkle_block_copy(kth_merkle_block_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_merkle_block_equals(kth_merkle_block_const_t self, kth_merkle_block_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_merkle_block_to_data(kth_merkle_block_const_t self, uint32_t version, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_merkle_block_serialized_size(kth_merkle_block_const_t self, uint32_t version);


// Getters

/** @return Borrowed `kth_header_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_header_const_t kth_chain_merkle_block_header(kth_merkle_block_const_t self);

KTH_EXPORT
kth_size_t kth_chain_merkle_block_total_transactions(kth_merkle_block_const_t self);

/** @return Borrowed `kth_hash_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_hash_list_const_t kth_chain_merkle_block_hashes(kth_merkle_block_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_merkle_block_flags(kth_merkle_block_const_t self, kth_size_t* out_size);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_merkle_block_set_header(kth_merkle_block_mut_t self, kth_header_const_t value);

KTH_EXPORT
void kth_chain_merkle_block_set_total_transactions(kth_merkle_block_mut_t self, kth_size_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_merkle_block_set_hashes(kth_merkle_block_mut_t self, kth_hash_list_const_t value);

KTH_EXPORT
void kth_chain_merkle_block_set_flags(kth_merkle_block_mut_t self, uint8_t const* value, kth_size_t n);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_merkle_block_is_valid(kth_merkle_block_const_t self);


// Operations

KTH_EXPORT
void kth_chain_merkle_block_reset(kth_merkle_block_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_MERKLE_BLOCK_H_
