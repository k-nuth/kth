// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_ENCRYPTED_KEYS_H_
#define KTH_CAPI_WALLET_ENCRYPTED_KEYS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

/**
 * @param token Borrowed input; must be non-null. Read during the call; ownership of `token` stays with the caller.
 * @param seed Borrowed input; must be non-null. Read during the call; ownership of `seed` stays with the caller.
 */
KTH_EXPORT
kth_bool_t kth_wallet_encrypted_keys_create_key_pair(kth_encrypted_private_t* out_private, kth_ec_compressed_t* out_point, kth_encrypted_token_t const* token, kth_ek_seed_t const* seed, uint8_t version, kth_bool_t compressed);

/**
 * @warning `token` MUST point to a buffer of at least 53 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_encrypted_token_t`.
 * @warning `seed` MUST point to a buffer of at least 24 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_ek_seed_t`.
 */
KTH_EXPORT
kth_bool_t kth_wallet_encrypted_keys_create_key_pair_unsafe(kth_encrypted_private_t* out_private, kth_ec_compressed_t* out_point, uint8_t const* token, uint8_t const* seed, uint8_t version, kth_bool_t compressed);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_ENCRYPTED_KEYS_H_
