// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_HEADER_H_
#define KTH_CAPI_CHAIN_HEADER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_header_mut_t`. Caller must release with `kth_chain_header_destruct`. */
KTH_EXPORT KTH_OWNED
kth_header_mut_t kth_chain_header_construct_default(void);

/** @param[out] out On success, owned `kth_header_mut_t`. Caller must release with `kth_chain_header_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_header_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_header_mut_t* out);

/** @return Owned `kth_header_mut_t`. Caller must release with `kth_chain_header_destruct`. */
KTH_EXPORT KTH_OWNED
kth_header_mut_t kth_chain_header_construct(uint32_t version, uint8_t const* previous_block_hash, uint8_t const* merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce);


// Destructor

KTH_EXPORT
void kth_chain_header_destruct(kth_header_mut_t self);


// Copy

/** @return Owned `kth_header_mut_t`. Caller must release with `kth_chain_header_destruct`. */
KTH_EXPORT KTH_OWNED
kth_header_mut_t kth_chain_header_copy(kth_header_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_header_equals(kth_header_const_t self, kth_header_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_header_to_data(kth_header_const_t self, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_header_serialized_size(kth_header_const_t self, kth_bool_t wire);


// Getters

KTH_EXPORT
kth_hash_t kth_chain_header_hash_pow(kth_header_const_t self);

KTH_EXPORT
uint32_t kth_chain_header_version(kth_header_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_header_previous_block_hash(kth_header_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_header_merkle(kth_header_const_t self);

KTH_EXPORT
uint32_t kth_chain_header_timestamp(kth_header_const_t self);

KTH_EXPORT
uint32_t kth_chain_header_bits(kth_header_const_t self);

KTH_EXPORT
uint32_t kth_chain_header_nonce(kth_header_const_t self);


// Setters

KTH_EXPORT
void kth_chain_header_set_version(kth_header_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_header_set_previous_block_hash(kth_header_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_header_set_merkle(kth_header_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_header_set_timestamp(kth_header_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_header_set_bits(kth_header_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_header_set_nonce(kth_header_mut_t self, uint32_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_header_is_valid_proof_of_work(kth_header_const_t self, kth_bool_t retarget);

KTH_EXPORT
kth_bool_t kth_chain_header_is_valid(kth_header_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_header_is_valid_timestamp(kth_header_const_t self);


// Operations

KTH_EXPORT
kth_error_code_t kth_chain_header_check(kth_header_const_t self, kth_bool_t retarget);

KTH_EXPORT
kth_error_code_t kth_chain_header_accept(kth_header_const_t self, kth_chain_state_const_t state);

KTH_EXPORT
void kth_chain_header_reset(kth_header_mut_t self);


// Static utilities

KTH_EXPORT
kth_size_t kth_chain_header_satoshi_fixed_size(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_HEADER_H_
