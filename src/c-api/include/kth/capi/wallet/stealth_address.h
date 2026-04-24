// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_STEALTH_ADDRESS_H_
#define KTH_CAPI_WALLET_STEALTH_ADDRESS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_stealth_address_mut_t`. Caller must release with `kth_wallet_stealth_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_construct_default(void);

/** @return Owned `kth_stealth_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_construct_from_decoded(uint8_t const* decoded, kth_size_t n);

/** @return Owned `kth_stealth_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_construct_from_encoded(char const* encoded);

/**
 * @return Owned `kth_stealth_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_address_destruct`.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @param spend_keys Borrowed input. Copied by value into the resulting object; ownership of `spend_keys` stays with the caller.
 * @param scan_key Borrowed input; must be non-null. Copied into the resulting object; ownership of `scan_key` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_construct_from_binary_scan_key_spend_keys_signatures_version(kth_binary_const_t filter, kth_ec_compressed_t const* scan_key, kth_ec_compressed_list_const_t spend_keys, uint8_t signatures, uint8_t version);

/**
 * @return Owned `kth_stealth_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_address_destruct`.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @param spend_keys Borrowed input. Copied by value into the resulting object; ownership of `spend_keys` stays with the caller.
 * @warning `scan_key` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_construct_from_binary_scan_key_spend_keys_signatures_version_unsafe(kth_binary_const_t filter, uint8_t const* scan_key, kth_ec_compressed_list_const_t spend_keys, uint8_t signatures, uint8_t version);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_stealth_address_destruct(kth_stealth_address_mut_t self);


// Copy

/** @return Owned `kth_stealth_address_mut_t`. Caller must release with `kth_wallet_stealth_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_stealth_address_copy(kth_stealth_address_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_stealth_address_equals(kth_stealth_address_const_t self, kth_stealth_address_const_t other);


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_address_valid(kth_stealth_address_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_stealth_address_encoded(kth_stealth_address_const_t self);

KTH_EXPORT
uint8_t kth_wallet_stealth_address_version(kth_stealth_address_const_t self);

KTH_EXPORT
kth_ec_compressed_t kth_wallet_stealth_address_scan_key(kth_stealth_address_const_t self);

/** @return Borrowed `kth_ec_compressed_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_ec_compressed_list_const_t kth_wallet_stealth_address_spend_keys(kth_stealth_address_const_t self);

KTH_EXPORT
uint8_t kth_wallet_stealth_address_signatures(kth_stealth_address_const_t self);

/** @return Borrowed `kth_binary_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_binary_const_t kth_wallet_stealth_address_filter(kth_stealth_address_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_wallet_stealth_address_to_chunk(kth_stealth_address_const_t self, kth_size_t* out_size);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_stealth_address_less(kth_stealth_address_const_t self, kth_stealth_address_const_t x);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_STEALTH_ADDRESS_H_
