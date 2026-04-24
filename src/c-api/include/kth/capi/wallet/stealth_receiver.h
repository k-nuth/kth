// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_STEALTH_RECEIVER_H_
#define KTH_CAPI_WALLET_STEALTH_RECEIVER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/**
 * @return Owned `kth_stealth_receiver_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_receiver_destruct`.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @param scan_private Borrowed input; must be non-null. Copied into the resulting object; ownership of `scan_private` stays with the caller.
 * @param spend_private Borrowed input; must be non-null. Copied into the resulting object; ownership of `spend_private` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_construct(kth_hash_t const* scan_private, kth_hash_t const* spend_private, kth_binary_const_t filter, uint8_t version);

/**
 * @return Owned `kth_stealth_receiver_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_receiver_destruct`.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @warning `scan_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @warning `spend_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_construct_unsafe(uint8_t const* scan_private, uint8_t const* spend_private, kth_binary_const_t filter, uint8_t version);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_stealth_receiver_destruct(kth_stealth_receiver_mut_t self);


// Copy

/** @return Owned `kth_stealth_receiver_mut_t`. Caller must release with `kth_wallet_stealth_receiver_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_copy(kth_stealth_receiver_const_t self);


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_receiver_valid(kth_stealth_receiver_const_t self);

/** @return Borrowed `kth_stealth_address_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_stealth_address_const_t kth_wallet_stealth_receiver_stealth_address(kth_stealth_receiver_const_t self);


// Operations

/** @param ephemeral_public Borrowed input; must be non-null. Read during the call; ownership of `ephemeral_public` stays with the caller. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_receiver_derive_address(kth_stealth_receiver_const_t self, kth_payment_address_mut_t out_address, kth_ec_compressed_t const* ephemeral_public);

/** @warning `ephemeral_public` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_receiver_derive_address_unsafe(kth_stealth_receiver_const_t self, kth_payment_address_mut_t out_address, uint8_t const* ephemeral_public);

/** @param ephemeral_public Borrowed input; must be non-null. Read during the call; ownership of `ephemeral_public` stays with the caller. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_receiver_derive_private(kth_stealth_receiver_const_t self, kth_hash_t* out_private, kth_ec_compressed_t const* ephemeral_public);

/** @warning `ephemeral_public` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_receiver_derive_private_unsafe(kth_stealth_receiver_const_t self, kth_hash_t* out_private, uint8_t const* ephemeral_public);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_STEALTH_RECEIVER_H_
