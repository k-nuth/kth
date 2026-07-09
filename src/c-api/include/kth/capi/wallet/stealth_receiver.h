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
 * @param scan_private Borrowed input; must be non-null. Read during the call; ownership of `scan_private` stays with the caller.
 * @param spend_private Borrowed input; must be non-null. Read during the call; ownership of `spend_private` stays with the caller.
 * @param[out] out Must point to a null `kth_stealth_receiver_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_receiver_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_from_secrets(kth_hash_t const* scan_private, kth_hash_t const* spend_private, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_receiver_mut_t* out);

/**
 * @warning `scan_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @warning `spend_private` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 * @param[out] out Must point to a null `kth_stealth_receiver_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_receiver_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_from_secrets_unsafe(uint8_t const* scan_private, uint8_t const* spend_private, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_receiver_mut_t* out);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_stealth_receiver_destruct(kth_stealth_receiver_mut_t self);


// Copy

/** @return Owned `kth_stealth_receiver_mut_t`. Caller must release with `kth_wallet_stealth_receiver_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_copy(kth_stealth_receiver_const_t self);


// Getters

/** @return Borrowed `kth_stealth_address_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_stealth_address_const_t kth_wallet_stealth_receiver_stealth_address(kth_stealth_receiver_const_t self);


// Operations

/**
 * @param ephemeral_public Borrowed input; must be non-null. Read during the call; ownership of `ephemeral_public` stays with the caller.
 * @param[out] out Must point to a null `kth_payment_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_payment_address_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_derive_address(kth_stealth_receiver_const_t self, kth_ec_compressed_t const* ephemeral_public, KTH_OUT_OWNED kth_payment_address_mut_t* out);

/**
 * @warning `ephemeral_public` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`.
 * @param[out] out Must point to a null `kth_payment_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_payment_address_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_derive_address_unsafe(kth_stealth_receiver_const_t self, uint8_t const* ephemeral_public, KTH_OUT_OWNED kth_payment_address_mut_t* out);

/**
 * @param ephemeral_public Borrowed input; must be non-null. Read during the call; ownership of `ephemeral_public` stays with the caller.
 * @warning `out` MUST point to a buffer of at least 32 bytes; `n` MUST be `>= 32`. Passing a shorter buffer aborts via the precondition check.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_derive_private(kth_stealth_receiver_const_t self, kth_ec_compressed_t const* ephemeral_public, uint8_t* out, kth_size_t n);

/**
 * @warning `ephemeral_public` MUST point to a buffer of at least 33 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ec_compressed_t`.
 * @warning `out` MUST point to a buffer of at least 32 bytes; `n` MUST be `>= 32`. Passing a shorter buffer aborts via the precondition check.
 */
KTH_EXPORT
kth_error_code_t kth_wallet_stealth_receiver_derive_private_unsafe(kth_stealth_receiver_const_t self, uint8_t const* ephemeral_public, uint8_t* out, kth_size_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_STEALTH_RECEIVER_H_
