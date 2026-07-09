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

/** @param[out] out Must point to a null `kth_stealth_sender_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_sender_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_sender_from_stealth_address(kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_sender_mut_t* out);

/**
 * @param ephemeral_private Borrowed input; must be non-null. Read during the call; ownership of `ephemeral_private` stays with the caller.
 * @param[out] out Must point to a null `kth_stealth_sender_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_sender_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_sender_from_ephemeral(kth_hash_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_sender_mut_t* out);

/**
 * @warning `ephemeral_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_stealth_sender_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_sender_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_sender_from_ephemeral_unsafe(uint8_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_sender_mut_t* out);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_stealth_sender_destruct(kth_stealth_sender_mut_t self);


// Copy

/** @return Owned `kth_stealth_sender_mut_t`. Caller must release with `kth_wallet_stealth_sender_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_sender_mut_t kth_wallet_stealth_sender_copy(kth_stealth_sender_const_t self);


// Getters

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
