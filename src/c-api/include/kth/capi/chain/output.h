// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_OUTPUT_H_
#define KTH_CAPI_CHAIN_OUTPUT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_output_mut_t`. Caller must release with `kth_chain_output_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_chain_output_construct_default(void);

/** @param[out] out Must point to a null `kth_output_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_output_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_output_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_output_mut_t* out);

/**
 * @return Owned `kth_output_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_output_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 * @param token_data Borrowed input. Copied by value into the resulting object; ownership of `token_data` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_chain_output_construct(uint64_t value, kth_script_const_t script, kth_token_data_const_t token_data);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_output_destruct(kth_output_mut_t self);


// Copy

/** @return Owned `kth_output_mut_t`. Caller must release with `kth_chain_output_destruct`. */
KTH_EXPORT KTH_OWNED
kth_output_mut_t kth_chain_output_copy(kth_output_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_output_equals(kth_output_const_t self, kth_output_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_output_to_data(kth_output_const_t self, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_output_serialized_size(kth_output_const_t self, kth_bool_t wire);


// Getters

KTH_EXPORT
uint64_t kth_chain_output_value(kth_output_const_t self);

/** @return Borrowed `kth_script_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_script_const_t kth_chain_output_script(kth_output_const_t self);

/** @return Borrowed `kth_token_data_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_token_data_const_t kth_chain_output_token_data(kth_output_const_t self);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_output_set_script(kth_output_mut_t self, kth_script_const_t value);

KTH_EXPORT
void kth_chain_output_set_value(kth_output_mut_t self, uint64_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_output_set_token_data(kth_output_mut_t self, kth_token_data_const_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_output_is_valid(kth_output_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_output_is_dust(kth_output_const_t self, uint64_t minimum_output_value);


// Operations

/** @return Owned `kth_payment_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_chain_output_address_simple(kth_output_const_t self, kth_bool_t testnet);

/** @return Owned `kth_payment_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_chain_output_address(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version);

/** @return Owned `kth_payment_address_list_mut_t`. Caller must release with `kth_wallet_payment_address_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_list_mut_t kth_chain_output_addresses(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version);

KTH_EXPORT
kth_size_t kth_chain_output_signature_operations(kth_output_const_t self, kth_bool_t bip141);

KTH_EXPORT
void kth_chain_output_reset(kth_output_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_OUTPUT_H_
