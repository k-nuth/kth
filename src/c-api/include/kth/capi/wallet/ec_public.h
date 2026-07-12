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

/** @param[out] out Must point to a null `kth_ec_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_public_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_public_construct_from_data(uint8_t const* decoded, kth_size_t n, KTH_OUT_OWNED kth_ec_public_mut_t* out);

/** @param[out] out Must point to a null `kth_ec_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_public_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_public_parse_from(char const* base16, KTH_OUT_OWNED kth_ec_public_mut_t* out);

/** @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_from_private(kth_ec_private_const_t secret);

/**
 * @param point Borrowed input; must be non-null. Read during the call; ownership of `point` stays with the caller.
 * @param[out] out Must point to a null `kth_ec_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_public_from_point(kth_ec_uncompressed_t const* point, kth_bool_t compress, KTH_OUT_OWNED kth_ec_public_mut_t* out);

/**
 * @warning `point` MUST point to a buffer of at least 65 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_uncompressed_t`.
 * @param[out] out Must point to a null `kth_ec_public_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ec_public_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_public_from_point_unsafe(uint8_t const* point, kth_bool_t compress, KTH_OUT_OWNED kth_ec_public_mut_t* out);

/**
 * @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`.
 * @param point Borrowed input; must be non-null. Read during the call; ownership of `point` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_from_verified_point(kth_ec_compressed_t const* point, kth_bool_t compress);

/**
 * @return Owned `kth_ec_public_mut_t`. Caller must release with `kth_wallet_ec_public_destruct`.
 * @warning `point` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`.
 */
KTH_EXPORT KTH_OWNED
kth_ec_public_mut_t kth_wallet_ec_public_from_verified_point_unsafe(uint8_t const* point, kth_bool_t compress);


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

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_not_equal(kth_ec_public_const_t self, kth_ec_public_const_t other);


// Ordering

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_less(kth_ec_public_const_t self, kth_ec_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_greater(kth_ec_public_const_t self, kth_ec_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_less_or_equal(kth_ec_public_const_t self, kth_ec_public_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_greater_or_equal(kth_ec_public_const_t self, kth_ec_public_const_t x);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_wallet_ec_public_to_data(kth_ec_public_const_t self, kth_size_t* out_size);


// Getters

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ec_public_encoded(kth_ec_public_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_ec_public_point(kth_ec_public_const_t self);

KTH_EXPORT
kth_bool_t kth_wallet_ec_public_compressed(kth_ec_public_const_t self);

KTH_EXPORT
kth_ec_uncompressed_t kth_wallet_ec_public_to_uncompressed(kth_ec_public_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_ec_public_value(kth_ec_public_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ec_public_to_string(kth_ec_public_const_t self);


// Operations

/** @param[out] out Must point to a null `kth_payment_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_payment_address_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ec_public_to_payment_address(kth_ec_public_const_t self, uint8_t version, KTH_OUT_OWNED kth_payment_address_mut_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_EC_PUBLIC_H_
