// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_TOKEN_DATA_H_
#define KTH_CAPI_CHAIN_TOKEN_DATA_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/token_capability.h>
#include <kth/capi/chain/token_kind.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @param[out] out Must point to a null `kth_token_data_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_token_data_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_token_data_construct_from_data(uint8_t const* data, kth_size_t n, KTH_OUT_OWNED kth_token_data_mut_t* out);


// Static factories

/** @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`. */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_fungible(kth_hash_t id, uint64_t amount);

/**
 * @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`.
 * @warning `id` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_fungible_unsafe(uint8_t const* id, uint64_t amount);

/** @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`. */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_non_fungible(kth_hash_t id, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n);

/**
 * @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`.
 * @warning `id` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_non_fungible_unsafe(uint8_t const* id, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n);

/** @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`. */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_both(kth_hash_t id, uint64_t amount, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n);

/**
 * @return Owned `kth_token_data_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_token_data_destruct`.
 * @warning `id` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_make_both_unsafe(uint8_t const* id, uint64_t amount, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_token_data_destruct(kth_token_data_mut_t self);


// Copy

/** @return Owned `kth_token_data_mut_t`. Caller must release with `kth_chain_token_data_destruct`. */
KTH_EXPORT KTH_OWNED
kth_token_data_mut_t kth_chain_token_data_copy(kth_token_data_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_token_data_equals(kth_token_data_const_t self, kth_token_data_const_t other);


// Serialization

KTH_EXPORT
kth_size_t kth_chain_token_data_serialized_size(kth_token_data_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_token_data_to_data(kth_token_data_const_t self, kth_size_t* out_size);


// Getters

KTH_EXPORT
kth_hash_t kth_chain_token_data_id(kth_token_data_const_t self);

KTH_EXPORT
kth_token_kind_t kth_chain_token_data_get_kind(kth_token_data_const_t self);

KTH_EXPORT
uint64_t kth_chain_token_data_get_amount(kth_token_data_const_t self);

KTH_EXPORT
kth_token_capability_t kth_chain_token_data_get_nft_capability(kth_token_data_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_token_data_get_nft_commitment(kth_token_data_const_t self, kth_size_t* out_size);

KTH_EXPORT
uint8_t kth_chain_token_data_bitfield(kth_token_data_const_t self);


// Setters

KTH_EXPORT
void kth_chain_token_data_set_id(kth_token_data_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_token_data_set_id_unsafe(kth_token_data_mut_t self, uint8_t const* value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_token_data_is_valid(kth_token_data_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_token_data_has_nft(kth_token_data_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_token_data_is_fungible_only(kth_token_data_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_token_data_is_immutable_nft(kth_token_data_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_token_data_is_mutable_nft(kth_token_data_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_token_data_is_minting_nft(kth_token_data_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_TOKEN_DATA_H_
