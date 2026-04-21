// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_HD_PUBLIC_H_
#define KTH_CAPI_WALLET_HD_PUBLIC_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>
#include <kth/capi/wallet/hd_lineage.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_hd_public_mut_t`. Caller must release with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_default(void);

/**
 * @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`.
 * @param public_key Borrowed input; must be non-null. Copied into the resulting object; ownership of `public_key` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key(kth_hd_key_t const* public_key);

/**
 * @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`.
 * @warning `public_key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hd_key_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_unsafe(uint8_t const* public_key);

/**
 * @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`.
 * @param public_key Borrowed input; must be non-null. Copied into the resulting object; ownership of `public_key` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix(kth_hd_key_t const* public_key, uint32_t prefix);

/**
 * @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`.
 * @warning `public_key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hd_key_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix_unsafe(uint8_t const* public_key, uint32_t prefix);

/** @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded(char const* encoded);

/** @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded_prefix(char const* encoded, uint32_t prefix);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_hd_public_destruct(kth_hd_public_mut_t self);


// Copy

/** @return Owned `kth_hd_public_mut_t`. Caller must release with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_copy(kth_hd_public_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_equals(kth_hd_public_const_t self, kth_hd_public_const_t other);


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_hd_public_valid(kth_hd_public_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_hd_public_encoded(kth_hd_public_const_t self);

KTH_EXPORT
kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self);

KTH_EXPORT
kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self);

KTH_EXPORT
kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x);

/** @return Owned `kth_hd_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_hd_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index);


// Static utilities

KTH_EXPORT
uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_HD_PUBLIC_H_
