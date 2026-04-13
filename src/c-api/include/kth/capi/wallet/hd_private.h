// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_HD_PRIVATE_H_
#define KTH_CAPI_WALLET_HD_PRIVATE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>
#include <kth/capi/wallet/hd_lineage.h>
#include <kth/capi/wallet/hd_public.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_default(void);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_seed_prefixes(uint8_t const* seed, kth_size_t n, uint64_t prefixes);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key(kth_hd_key_t private_key);

/**
 * @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`.
 * @warning `private_key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_unsafe(uint8_t const* private_key);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefixes(kth_hd_key_t private_key, uint64_t prefixes);

/**
 * @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`.
 * @warning `private_key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefixes_unsafe(uint8_t const* private_key, uint64_t prefixes);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefix(kth_hd_key_t private_key, uint32_t prefix);

/**
 * @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`.
 * @warning `private_key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefix_unsafe(uint8_t const* private_key, uint32_t prefix);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded(char const* encoded);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded_prefixes(char const* encoded, uint64_t prefixes);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded_prefix(char const* encoded, uint32_t prefix);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_hd_private_destruct(kth_hd_private_mut_t self);


// Copy

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_copy(kth_hd_private_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_hd_private_equals(kth_hd_private_const_t self, kth_hd_private_const_t other);


// Getters

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_hd_private_encoded(kth_hd_private_const_t self);

KTH_EXPORT
kth_hash_t kth_wallet_hd_private_secret(kth_hd_private_const_t self);

KTH_EXPORT
kth_hd_key_t kth_wallet_hd_private_to_hd_key(kth_hd_private_const_t self);

/** @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_private_to_public(kth_hd_private_const_t self);

KTH_EXPORT
kth_bool_t kth_wallet_hd_private_valid(kth_hd_private_const_t self);

KTH_EXPORT
kth_hash_t kth_wallet_hd_private_chain_code(kth_hd_private_const_t self);

KTH_EXPORT
kth_hd_lineage_t kth_wallet_hd_private_lineage(kth_hd_private_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_hd_private_point(kth_hd_private_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_hd_private_less(kth_hd_private_const_t self, kth_hd_private_const_t x);

/** @return Owned `kth_hd_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_private_mut_t kth_wallet_hd_private_derive_private(kth_hd_private_const_t self, uint32_t index);

/** @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_private_derive_public(kth_hd_private_const_t self, uint32_t index);


// Static utilities

KTH_EXPORT
uint32_t kth_wallet_hd_private_to_prefix(uint64_t prefixes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_HD_PRIVATE_H_
