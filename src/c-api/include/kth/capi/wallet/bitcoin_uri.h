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

/** @param[out] out Must point to a null `kth_bitcoin_uri_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_bitcoin_uri_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_bitcoin_uri_parse_from(char const* uri, kth_bool_t strict, KTH_OUT_OWNED kth_bitcoin_uri_mut_t* out);


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

KTH_EXPORT
kth_bool_t kth_wallet_bitcoin_uri_not_equal(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t other);


// Getters

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

/** @param[out] out Must point to a null `kth_payment_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_payment_address_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_bitcoin_uri_payment(kth_bitcoin_uri_const_t self, KTH_OUT_OWNED kth_payment_address_mut_t* out);

/** @param[out] out Must point to a null `kth_stealth_address_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_stealth_address_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_bitcoin_uri_stealth(kth_bitcoin_uri_const_t self, KTH_OUT_OWNED kth_stealth_address_mut_t* out);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_to_string(kth_bitcoin_uri_const_t self);


// Operations

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_bitcoin_uri_parameter(kth_bitcoin_uri_const_t self, char const* key);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_BITCOIN_URI_H_
