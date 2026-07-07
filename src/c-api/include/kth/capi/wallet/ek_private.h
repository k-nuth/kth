// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_EK_PRIVATE_H_
#define KTH_CAPI_WALLET_EK_PRIVATE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_ek_private_mut_t`. Caller must release with `kth_wallet_ek_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ek_private_mut_t kth_wallet_ek_private_construct_default(void);

/**
 * @return Owned `kth_ek_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ek_private_destruct`.
 * @param value Borrowed input; must be non-null. Copied into the resulting object; ownership of `value` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_ek_private_mut_t kth_wallet_ek_private_construct(kth_encrypted_private_t const* value);

/**
 * @return Owned `kth_ek_private_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_ek_private_destruct`.
 * @warning `value` MUST point to a buffer of at least 43 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_encrypted_private_t`.
 */
KTH_EXPORT KTH_OWNED
kth_ek_private_mut_t kth_wallet_ek_private_construct_unsafe(uint8_t const* value);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_ek_private_destruct(kth_ek_private_mut_t self);


// Copy

/** @return Owned `kth_ek_private_mut_t`. Caller must release with `kth_wallet_ek_private_destruct`. */
KTH_EXPORT KTH_OWNED
kth_ek_private_mut_t kth_wallet_ek_private_copy(kth_ek_private_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_equals(kth_ek_private_const_t self, kth_ek_private_const_t other);

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_not_equal(kth_ek_private_const_t self, kth_ek_private_const_t other);


// Ordering

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_less(kth_ek_private_const_t self, kth_ek_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_greater(kth_ek_private_const_t self, kth_ek_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_less_or_equal(kth_ek_private_const_t self, kth_ek_private_const_t x);

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_greater_or_equal(kth_ek_private_const_t self, kth_ek_private_const_t x);


// Getters

KTH_EXPORT
kth_bool_t kth_wallet_ek_private_valid(kth_ek_private_const_t self);

KTH_EXPORT
kth_encrypted_private_t kth_wallet_ek_private_private_key(kth_ek_private_const_t self);

KTH_EXPORT
kth_encrypted_private_t kth_wallet_ek_private_value(kth_ek_private_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_ek_private_to_string(kth_ek_private_const_t self);


// Static utilities

/** @param[out] out Must point to a null `kth_ek_private_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_wallet_ek_private_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_wallet_ek_private_parse_from(char const* encoded, KTH_OUT_OWNED kth_ek_private_mut_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_EK_PRIVATE_H_
