// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_PAYMENT_ADDRESS_H_
#define KTH_CAPI_WALLET_PAYMENT_ADDRESS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_default(void);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded(kth_payment_t decoded);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @warning `decoded` MUST point to a buffer of at least 25 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded_unsafe(uint8_t const* decoded);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @param secret Borrowed input. Copied by value into the resulting object; ownership of `secret` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_private(kth_ec_private_const_t secret);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address(char const* address);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address_net(char const* address, kth_network_t net);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version(kth_shorthash_t short_hash, uint8_t version);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @warning `short_hash` MUST point to a buffer of at least 20 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version_unsafe(uint8_t const* short_hash, uint8_t version);

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version(kth_hash_t hash, uint8_t version);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @warning `hash` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version_unsafe(uint8_t const* hash, uint8_t version);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @param point Borrowed input. Copied by value into the resulting object; ownership of `point` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_public_version(kth_ec_public_const_t point, uint8_t version);

/**
 * @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_construct_from_script_version(kth_script_const_t script, uint8_t version);


// Static factories

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_from_pay_public_key_hash_script(kth_script_const_t script, uint8_t version);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_payment_address_destruct(kth_payment_address_mut_t self);


// Copy

/** @return Owned `kth_payment_address_mut_t`. Caller must release with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_payment_address_copy(kth_payment_address_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_payment_address_equals(kth_payment_address_const_t self, kth_payment_address_const_t other);


// Getters

KTH_EXPORT
kth_bool_t kth_wallet_payment_address_valid(kth_payment_address_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_payment_address_encoded_legacy(kth_payment_address_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_payment_address_encoded_token(kth_payment_address_const_t self);

KTH_EXPORT
uint8_t kth_wallet_payment_address_version(kth_payment_address_const_t self);

/** @return Borrowed pointer into `self`. Do not free. Length is written to `out_size`. Invalidated by any mutation of `self`. */
KTH_EXPORT
uint8_t const* kth_wallet_payment_address_hash_span(kth_payment_address_const_t self, kth_size_t* out_size);

KTH_EXPORT
kth_shorthash_t kth_wallet_payment_address_hash20(kth_payment_address_const_t self);

KTH_EXPORT
kth_hash_t kth_wallet_payment_address_hash32(kth_payment_address_const_t self);

KTH_EXPORT
kth_payment_t kth_wallet_payment_address_to_payment(kth_payment_address_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_payment_address_less(kth_payment_address_const_t self, kth_payment_address_const_t x);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_payment_address_encoded_cashaddr(kth_payment_address_const_t self, kth_bool_t token_aware);


// Static utilities

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_payment_address_cashaddr_prefix_for(kth_network_t net);

/** @return Owned `kth_payment_address_list_mut_t`. Caller must release with `kth_wallet_payment_address_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_list_mut_t kth_wallet_payment_address_extract(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version);

/** @return Owned `kth_payment_address_list_mut_t`. Caller must release with `kth_wallet_payment_address_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_list_mut_t kth_wallet_payment_address_extract_input(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version);

/** @return Owned `kth_payment_address_list_mut_t`. Caller must release with `kth_wallet_payment_address_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_list_mut_t kth_wallet_payment_address_extract_output(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_PAYMENT_ADDRESS_H_
