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

/** @return Owned `kth_ec_private_mut_t`. Caller must release with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_default(void);

/** @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_version(char const* wif, uint8_t version);

/** @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version(kth_wif_compressed_t wif_compressed, uint8_t version);

/**
 * @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`.
 * @warning `wif_compressed` MUST point to a buffer of at least 38 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version_unsafe(uint8_t const* wif_compressed, uint8_t version);

/** @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version(kth_wif_uncompressed_t wif_uncompressed, uint8_t version);

/**
 * @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`.
 * @warning `wif_uncompressed` MUST point to a buffer of at least 37 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version_unsafe(uint8_t const* wif_uncompressed, uint8_t version);

/** @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress(kth_hash_t secret, uint16_t version, kth_bool_t compress);

/**
 * @return Owned `kth_ec_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_private_destruct`.
 * @warning `secret` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress);


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


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_ec_private_valid(kth_ec_private_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ec_private_encoded(kth_ec_private_const_t self);

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

/** @return Owned `kth_ec_public_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_private_to_public(kth_ec_private_const_t self);

/** @return Owned `kth_payment_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_ec_private_to_payment_address(kth_ec_private_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_ec_private_less(kth_ec_private_const_t self, kth_ec_private_const_t x);


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
