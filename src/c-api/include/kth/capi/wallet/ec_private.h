// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_EC_PRIVATE_H_
#define KTH_CAPI_WALLET_EC_PRIVATE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_parse_from(char const* wif, uint8_t version, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @param wif Borrowed input; must be non-null. Read during the call; ownership of `wif` stays with the caller.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_compressed(kth_wif_compressed_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @warning `wif` MUST point to a buffer of at least 38 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_wif_compressed_t`.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_compressed_unsafe(uint8_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @param wif Borrowed input; must be non-null. Read during the call; ownership of `wif` stays with the caller.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_uncompressed(kth_wif_uncompressed_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @warning `wif` MUST point to a buffer of at least 37 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_wif_uncompressed_t`.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_uncompressed_unsafe(uint8_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @param secret Borrowed input; must be non-null. Read during the call; ownership of `secret` stays with the caller.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_secret(kth_hash_t const* secret, uint16_t version, kth_bool_t compress, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @warning `secret` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_ec_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_private_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_from_secret_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress, KTH_OUT_OWNED kth_ec_private_mut_t* out);

/**
 * @return Owned `kth_ec_private_mut_t`. Caller must release with `kth_wallet_ec_private_destruct`.
 * @param secret Borrowed input; must be non-null. Read during the call; ownership of `secret` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_from_verified_secret(kth_hash_t const* secret, uint16_t version, kth_bool_t compress);

/**
 * @return Owned `kth_ec_private_mut_t`. Caller must release with `kth_wallet_ec_private_destruct`.
 * @warning `secret` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_from_verified_secret_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_ec_private_destruct(kth_ec_private_mut_t self);


// Copy

/** @return Owned `kth_ec_private_mut_t`. Caller must release with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_copy(kth_ec_private_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_equals(kth_ec_private_const_t self, kth_ec_private_const_t other);

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_not_equal(kth_ec_private_const_t self, kth_ec_private_const_t other);


// Ordering

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_less(kth_ec_private_const_t self, kth_ec_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_greater(kth_ec_private_const_t self, kth_ec_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_less_or_equal(kth_ec_private_const_t self, kth_ec_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_greater_or_equal(kth_ec_private_const_t self, kth_ec_private_const_t x);


// Getters

KTH_EXPORT
kth_hash_t kth_wallet_ec_private_secret(kth_ec_private_const_t self);

KTH_EXPORT
uint16_t kth_wallet_ec_private_version(kth_ec_private_const_t self);

KTH_EXPORT
uint8_t kth_wallet_ec_private_payment_version(kth_ec_private_const_t self);

KTH_EXPORT
uint8_t kth_wallet_ec_private_wif_version(kth_ec_private_const_t self);

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_compressed(kth_ec_private_const_t self);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_private_to_public(kth_ec_private_const_t self);

/** @param[out] out Must point to a null `kth_payment_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_payment_address_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_private_to_payment_address(kth_ec_private_const_t self, KTH_OUT_OWNED kth_payment_address_mut_t* out);

KTH_EXPORT
kth_hash_t kth_wallet_ec_private_value(kth_ec_private_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ec_private_to_string(kth_ec_private_const_t self);


// Static utilities

KTH_EXPORT
uint8_t kth_wallet_ec_private_to_address_prefix(uint16_t version);

KTH_EXPORT
uint8_t kth_wallet_ec_private_to_wif_prefix(uint16_t version);

KTH_EXPORT
uint16_t kth_wallet_ec_private_to_version(uint8_t address, uint8_t wif);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_EC_PRIVATE_H_
