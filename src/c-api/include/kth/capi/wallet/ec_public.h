// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_EC_PUBLIC_H_
#define KTH_CAPI_WALLET_EC_PUBLIC_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_default(void);

/**
 * @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`.
 * @param secret Borrowed input. Copied by value into the resulting object; ownership of `secret` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_ec_private(kth_ec_private_const_t secret);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_decoded(uint8_t const* decoded, kth_size_t n);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_base16(char const* base16);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress(kth_ec_compressed_t compressed_point, kth_bool_t compress);

/**
 * @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`.
 * @warning `compressed_point` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress_unsafe(uint8_t const* compressed_point, kth_bool_t compress);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress(kth_ec_uncompressed_t uncompressed_point, kth_bool_t compress);

/**
 * @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`.
 * @warning `uncompressed_point` MUST point to a buffer of at least 65 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress_unsafe(uint8_t const* uncompressed_point, kth_bool_t compress);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_ec_public_destruct(kth_ec_public_mut_t self);


// Copy

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_copy(kth_ec_public_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_equals(kth_ec_public_const_t self, kth_ec_public_const_t other);


// Serialization

/** @return Owned byte buffer (33 bytes compressed, 65 uncompressed). Caller must release with `kth_core_destruct_array`. */
KTH_EXPORT KTH_OWNED
uint8_t* kth_wallet_ec_public_to_data(kth_ec_public_const_t self, kth_size_t* out_size);


// Getters

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ec_public_encoded(kth_ec_public_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_ec_public_point(kth_ec_public_const_t self);

KTH_EXPORT
uint16_t kth_wallet_ec_public_version(kth_ec_public_const_t self);

KTH_EXPORT
uint8_t kth_wallet_ec_public_payment_version(kth_ec_public_const_t self);

KTH_EXPORT
uint8_t kth_wallet_ec_public_wif_version(kth_ec_public_const_t self);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_compressed(kth_ec_public_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_less(kth_ec_public_const_t self, kth_ec_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_to_uncompressed(kth_ec_public_const_t self, uint8_t* out);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_ec_public_to_payment_address(kth_ec_public_const_t self, uint8_t version);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_EC_PUBLIC_H_
