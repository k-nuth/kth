// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_STEALTH_SENDER_H_
#define KTH_CAPI_WALLET_STEALTH_SENDER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/**
 * @return Owned `kth_stealth_sender_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_sender_destruct`.
 * @param address Borrowed input. Copied by value into the resulting object; ownership of `address` stays with the caller.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_stealth_address_seed_binary_version(kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version);

/**
 * @return Owned `kth_stealth_sender_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_sender_destruct`.
 * @param address Borrowed input. Copied by value into the resulting object; ownership of `address` stays with the caller.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @param ephemeral_private Borrowed input; must be non-null. Copied into the resulting object; ownership of `ephemeral_private` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(kth_hash_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version);

/**
 * @return Owned `kth_stealth_sender_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_sender_destruct`.
 * @param address Borrowed input. Copied by value into the resulting object; ownership of `address` stays with the caller.
 * @param filter Borrowed input. Copied by value into the resulting object; ownership of `filter` stays with the caller.
 * @warning `ephemeral_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version_unsafe(uint8_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_stealth_sender_destruct(kth_stealth_sender_mut_t self);


// Copy

/** @return Owned `kth_stealth_sender_mut_t`. Caller must release with `kth_wallet_stealth_sender_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_sender_mut_t kth_wallet_stealth_sender_copy(kth_stealth_sender_const_t self);


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_stealth_sender_valid(kth_stealth_sender_const_t self);

/** @return Borrowed `kth_script_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_script_const_t kth_wallet_stealth_sender_stealth_script(kth_stealth_sender_const_t self);

/** @return Borrowed `kth_payment_address_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_payment_address_const_t kth_wallet_stealth_sender_payment_address(kth_stealth_sender_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_STEALTH_SENDER_H_
