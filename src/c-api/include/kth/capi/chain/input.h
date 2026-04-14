// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_INPUT_H_
#define KTH_CAPI_CHAIN_INPUT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_input_mut_t`. Caller must release with `kth_chain_input_destruct`. */
KTH_EXPORT KTH_OWNED
kth_input_mut_t kth_chain_input_construct_default(void);

/** @param[out] out Must point to a null `kth_input_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_input_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_input_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_input_mut_t* out);

/**
 * @return Owned `kth_input_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_input_destruct`.
 * @param previous_output Borrowed input. Copied by value into the resulting object; ownership of `previous_output` stays with the caller.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_input_mut_t kth_chain_input_construct(kth_output_point_const_t previous_output, kth_script_const_t script, uint32_t sequence);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_input_destruct(kth_input_mut_t self);


// Copy

/** @return Owned `kth_input_mut_t`. Caller must release with `kth_chain_input_destruct`. */
KTH_EXPORT KTH_OWNED
kth_input_mut_t kth_chain_input_copy(kth_input_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_input_equals(kth_input_const_t self, kth_input_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_input_to_data(kth_input_const_t self, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_input_serialized_size(kth_input_const_t self, kth_bool_t wire);


// Getters

/** @return Owned `kth_payment_address_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_wallet_payment_address_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_mut_t kth_chain_input_address(kth_input_const_t self);

/** @return Owned `kth_payment_address_list_mut_t`. Caller must release with `kth_wallet_payment_address_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_payment_address_list_mut_t kth_chain_input_addresses(kth_input_const_t self);

/** @return Borrowed `kth_output_point_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_output_point_const_t kth_chain_input_previous_output(kth_input_const_t self);

/** @return Borrowed `kth_script_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_script_const_t kth_chain_input_script(kth_input_const_t self);

KTH_EXPORT
uint32_t kth_chain_input_sequence(kth_input_const_t self);

/** @param[out] out Must point to a null `kth_script_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_script_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_input_extract_embedded_script(kth_input_const_t self, KTH_OUT_OWNED kth_script_mut_t* out);


// Setters

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_input_set_script(kth_input_mut_t self, kth_script_const_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_input_set_previous_output(kth_input_mut_t self, kth_output_point_const_t value);

KTH_EXPORT
void kth_chain_input_set_sequence(kth_input_mut_t self, uint32_t value);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_input_is_valid(kth_input_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_input_is_final(kth_input_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_input_is_locked(kth_input_const_t self, kth_size_t block_height, uint32_t median_time_past);


// Operations

KTH_EXPORT
void kth_chain_input_reset(kth_input_mut_t self);

KTH_EXPORT
kth_size_t kth_chain_input_signature_operations(kth_input_const_t self, kth_bool_t bip16, kth_bool_t bip141);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_INPUT_H_
