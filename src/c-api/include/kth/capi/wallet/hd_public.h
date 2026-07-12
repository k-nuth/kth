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

/** @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_parse_from(char const* encoded, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/** @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_parse_from_with_prefix(char const* encoded, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @param key Borrowed input; must be non-null. Read during the call; ownership of `key` stays with the caller.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_hd_key(kth_hd_key_t const* key, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @warning `key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hd_key_t`.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_hd_key_unsafe(uint8_t const* key, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @param key Borrowed input; must be non-null. Read during the call; ownership of `key` stays with the caller.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_hd_key_with_prefix(kth_hd_key_t const* key, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @warning `key` MUST point to a buffer of at least 82 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hd_key_t`.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_hd_key_with_prefix_unsafe(uint8_t const* key, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @param secret Borrowed input; must be non-null. Read during the call; ownership of `secret` stays with the caller.
 * @param chain_code Borrowed input; must be non-null. Read during the call; ownership of `chain_code` stays with the caller.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_secret(kth_hash_t const* secret, kth_hash_t const* chain_code, kth_hd_lineage_t lineage, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @warning `secret` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @warning `chain_code` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_from_secret_unsafe(uint8_t const* secret, uint8_t const* chain_code, kth_hd_lineage_t lineage, KTH_OUT_OWNED kth_hd_public_mut_t* out);

/**
 * @return Owned `kth_hd_public_mut_t`. Caller must release with `kth_wallet_hd_public_destruct`.
 * @param point Borrowed input; must be non-null. Read during the call; ownership of `point` stays with the caller.
 * @param chain_code Borrowed input; must be non-null. Read during the call; ownership of `chain_code` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_from_verified_components(kth_ec_compressed_t const* point, kth_hash_t const* chain_code, kth_hd_lineage_t lineage);

/**
 * @return Owned `kth_hd_public_mut_t`. Caller must release with `kth_wallet_hd_public_destruct`.
 * @warning `point` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`.
 * @warning `chain_code` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hd_public_mut_t kth_wallet_hd_public_from_verified_components_unsafe(uint8_t const* point, uint8_t const* chain_code, kth_hd_lineage_t lineage);


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

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_not_equal(kth_hd_public_const_t self, kth_hd_public_const_t other);


// Ordering

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_greater(kth_hd_public_const_t self, kth_hd_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_less_or_equal(kth_hd_public_const_t self, kth_hd_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_hd_public_greater_or_equal(kth_hd_public_const_t self, kth_hd_public_const_t x);


// Getters

KTH_EXPORT
kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self);

KTH_EXPORT
kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self);

KTH_EXPORT
kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_hd_public_to_string(kth_hd_public_const_t self);

KTH_EXPORT
uint32_t kth_wallet_hd_public_fingerprint(kth_hd_public_const_t self);


// Operations

/** @param[out] out Must point to a null `kth_hd_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_hd_public_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index, KTH_OUT_OWNED kth_hd_public_mut_t* out);

KTH_EXPORT
void kth_wallet_hd_public_wipe(kth_hd_public_mut_t self);


// Static utilities

KTH_EXPORT
uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_HD_PUBLIC_H_
