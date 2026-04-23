// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_MESSAGE_H_
#define KTH_CAPI_WALLET_MESSAGE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

KTH_EXPORT
kth_hash_t kth_wallet_message_hash_message(uint8_t const* message, kth_size_t n);

KTH_EXPORT
kth_bool_t kth_wallet_message_sign_message_ec_private(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, kth_ec_private_const_t secret);

KTH_EXPORT
kth_bool_t kth_wallet_message_sign_message_string(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, char const* wif);

/** @param secret Borrowed input; must be non-null. Read during the call; ownership of `secret` stays with the caller. */
KTH_EXPORT
kth_bool_t kth_wallet_message_sign_message_hash(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, kth_hash_t const* secret, kth_bool_t compressed);

/** @warning `secret` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`. */
KTH_EXPORT
kth_bool_t kth_wallet_message_sign_message_hash_unsafe(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, uint8_t const* secret, kth_bool_t compressed);

/** @param signature Borrowed input; must be non-null. Read during the call; ownership of `signature` stays with the caller. */
KTH_EXPORT
kth_bool_t kth_wallet_message_verify_message(uint8_t const* message, kth_size_t n, kth_payment_address_const_t address, kth_message_signature_t const* signature);

/** @warning `signature` MUST point to a buffer of at least 65 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_message_signature_t`. */
KTH_EXPORT
kth_bool_t kth_wallet_message_verify_message_unsafe(uint8_t const* message, kth_size_t n, kth_payment_address_const_t address, uint8_t const* signature);

KTH_EXPORT
kth_bool_t kth_wallet_message_recovery_id_to_magic(uint8_t* out_magic, uint8_t recovery_id, kth_bool_t compressed);

KTH_EXPORT
kth_bool_t kth_wallet_message_magic_to_recovery_id(uint8_t* out_recovery_id, kth_bool_t* out_compressed, uint8_t magic);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_MESSAGE_H_
