// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_COMPACT_BLOCK_H_
#define KTH_CAPI_CHAIN_COMPACT_BLOCK_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @param[out] out Must point to a null `kth_compact_block_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_compact_block_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_compact_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_compact_block_mut_t* out);

/** @param[out] out Must point to a null `kth_compact_block_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_compact_block_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_compact_block_create(kth_header_const_t header, uint64_t nonce, kth_u64_list_const_t short_ids, kth_prefilled_transaction_list_const_t transactions, KTH_OUT_OWNED kth_compact_block_mut_t* out);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_compact_block_destruct(kth_compact_block_mut_t self);


// Copy

/** @return Owned `kth_compact_block_mut_t`. Caller must release with `kth_chain_compact_block_destruct`. */
KTH_EXPORT KTH_OWNED
kth_compact_block_mut_t kth_chain_compact_block_copy(kth_compact_block_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_compact_block_equals(kth_compact_block_const_t self, kth_compact_block_const_t other);

KTH_EXPORT
kth_bool_t kth_chain_compact_block_not_equal(kth_compact_block_const_t self, kth_compact_block_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_compact_block_to_data(kth_compact_block_const_t self, uint32_t version, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_compact_block_serialized_size(kth_compact_block_const_t self, uint32_t version);


// Getters

/** @return Borrowed `kth_header_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_header_const_t kth_chain_compact_block_header(kth_compact_block_const_t self);

KTH_EXPORT
uint64_t kth_chain_compact_block_nonce(kth_compact_block_const_t self);

/** @return Owned `kth_u64_list_mut_t`. Caller must release with `kth_core_u64_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_u64_list_mut_t kth_chain_compact_block_short_ids(kth_compact_block_const_t self);

/** @return Borrowed `kth_prefilled_transaction_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_prefilled_transaction_list_const_t kth_chain_compact_block_transactions(kth_compact_block_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_COMPACT_BLOCK_H_
