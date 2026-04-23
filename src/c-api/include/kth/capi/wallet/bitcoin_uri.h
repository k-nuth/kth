// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_BITCOIN_URI_H_
#define KTH_CAPI_WALLET_BITCOIN_URI_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_bitcoin_uri_mut_t`. Caller must release with `kth_wallet_bitcoin_uri_destruct`. */
KTH_EXPORT KTH_OWNED
kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_construct_default(void);

/** @return Owned `kth_bitcoin_uri_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_bitcoin_uri_destruct`. */
KTH_EXPORT KTH_OWNED
kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_construct(char const* uri, kth_bool_t strict);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_bitcoin_uri_destruct(kth_bitcoin_uri_mut_t self);


// Copy

/** @return Owned `kth_bitcoin_uri_mut_t`. Caller must release with `kth_wallet_bitcoin_uri_destruct`. */
KTH_EXPORT KTH_OWNED
kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_copy(kth_bitcoin_uri_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_equals(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t other);


// Getters

/** @return Non-zero if `self` is in a valid state, zero otherwise. */
KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_valid(kth_bitcoin_uri_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_encoded(kth_bitcoin_uri_const_t self);

KTH_EXPORT
uint64_t kth_wallet_bitcoin_uri_amount(kth_bitcoin_uri_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_label(kth_bitcoin_uri_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_message(kth_bitcoin_uri_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_r(kth_bitcoin_uri_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_address(kth_bitcoin_uri_const_t self);

/** @return Owned `kth_payment_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_wallet_bitcoin_uri_payment(kth_bitcoin_uri_const_t self);

/** @return Owned `kth_stealth_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_stealth_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_address_mut_t kth_wallet_bitcoin_uri_stealth(kth_bitcoin_uri_const_t self);


// Setters

KTH_EXPORT
void kth_wallet_bitcoin_uri_set_amount(kth_bitcoin_uri_mut_t self, uint64_t satoshis);

KTH_EXPORT
void kth_wallet_bitcoin_uri_set_label(kth_bitcoin_uri_mut_t self, char const* label);

KTH_EXPORT
void kth_wallet_bitcoin_uri_set_message(kth_bitcoin_uri_mut_t self, char const* message);

KTH_EXPORT
void kth_wallet_bitcoin_uri_set_r(kth_bitcoin_uri_mut_t self, char const* r);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_address_string(kth_bitcoin_uri_mut_t self, char const* address);

/** @param payment Borrowed input. Copied by value into the resulting object; ownership of `payment` stays with the caller. */
KTH_EXPORT
void kth_wallet_bitcoin_uri_set_address_payment_address(kth_bitcoin_uri_mut_t self, kth_payment_address_const_t payment);

/** @param stealth Borrowed input. Copied by value into the resulting object; ownership of `stealth` stays with the caller. */
KTH_EXPORT
void kth_wallet_bitcoin_uri_set_address_stealth_address(kth_bitcoin_uri_mut_t self, kth_stealth_address_const_t stealth);

KTH_EXPORT
void kth_wallet_bitcoin_uri_set_strict(kth_bitcoin_uri_mut_t self, kth_bool_t strict);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_scheme(kth_bitcoin_uri_mut_t self, char const* scheme);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_authority(kth_bitcoin_uri_mut_t self, char const* authority);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_path(kth_bitcoin_uri_mut_t self, char const* path);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_fragment(kth_bitcoin_uri_mut_t self, char const* fragment);

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_set_parameter(kth_bitcoin_uri_mut_t self, char const* key, char const* value);


// Operations

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_less(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t x);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_parameter(kth_bitcoin_uri_const_t self, char const* key);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_BITCOIN_URI_H_
