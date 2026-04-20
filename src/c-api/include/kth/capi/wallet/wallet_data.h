// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_WALLET_DATA_H_
#define KTH_CAPI_WALLET_WALLET_DATA_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_wallet_data_destruct(kth_wallet_data_mut_t self);


// Copy

/** @return Owned `kth_wallet_data_mut_t`. Caller must release with `kth_wallet_wallet_data_destruct`. */
KTH_EXPORT KTH_OWNED
kth_wallet_data_mut_t kth_wallet_wallet_data_copy(kth_wallet_data_const_t self);


// Getters

/** @return Owned `kth_string_list_mut_t`. Caller must release with `kth_core_string_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_string_list_mut_t kth_wallet_wallet_data_mnemonics(kth_wallet_data_const_t self);

/** @return Borrowed `kth_hd_public_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_hd_public_const_t kth_wallet_wallet_data_xpub(kth_wallet_data_const_t self);

KTH_EXPORT
kth_encrypted_seed_t kth_wallet_wallet_data_encrypted_seed(kth_wallet_data_const_t self);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_wallet_wallet_data_set_mnemonics(kth_wallet_data_mut_t self, kth_string_list_const_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_wallet_wallet_data_set_xpub(kth_wallet_data_mut_t self, kth_hd_public_const_t value);

KTH_EXPORT
void kth_wallet_wallet_data_set_encrypted_seed(kth_wallet_data_mut_t self, kth_encrypted_seed_t value);

/** @warning `value` MUST point to a buffer of at least 96 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_wallet_wallet_data_set_encrypted_seed_unsafe(kth_wallet_data_mut_t self, uint8_t const* value);


// Static utilities

/** @param[out] out Must point to a null `kth_wallet_data_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_wallet_data_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_create(char const* password, char const* normalized_passphrase, KTH_OUT_OWNED kth_wallet_data_mut_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_WALLET_DATA_H_
