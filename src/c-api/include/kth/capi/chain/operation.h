// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_OPERATION_H_
#define KTH_CAPI_CHAIN_OPERATION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/opcode.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_operation_mut_t`. Caller must release with `kth_chain_operation_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_mut_t kth_chain_operation_construct_default(void);

/** @param[out] out Must point to a null `kth_operation_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_operation_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_operation_construct_from_data(uint8_t const* data, kth_size_t n, KTH_OUT_OWNED kth_operation_mut_t* out);

/** @return Owned `kth_operation_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_operation_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_mut_t kth_chain_operation_construct_from_uncoded_minimal(uint8_t const* uncoded, kth_size_t n, kth_bool_t minimal);

/** @return Owned `kth_operation_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_chain_operation_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_mut_t kth_chain_operation_construct_from_code(kth_opcode_t code);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_operation_destruct(kth_operation_mut_t self);


// Copy

/** @return Owned `kth_operation_mut_t`. Caller must release with `kth_chain_operation_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_mut_t kth_chain_operation_copy(kth_operation_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_operation_equals(kth_operation_const_t self, kth_operation_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_operation_to_data(kth_operation_const_t self, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_operation_serialized_size(kth_operation_const_t self);


// Getters

KTH_EXPORT
kth_opcode_t kth_chain_operation_code(kth_operation_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_operation_data(kth_operation_const_t self, kth_size_t* out_size);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_operation_is_valid(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_push(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_payload(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_counted(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_version(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_numeric(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_positive(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_reserved(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_disabled(kth_opcode_t code, kth_script_flags_t active_flags);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_conditional(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_relaxed_push(kth_opcode_t code);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_push_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_counted_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_version_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_positive_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_disabled_simple(kth_operation_const_t self, kth_script_flags_t active_flags);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_conditional_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_relaxed_push_simple(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_oversized(kth_operation_const_t self, kth_size_t max_size);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_minimal_push(kth_operation_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_operation_is_nominal_push(kth_operation_const_t self);


// Operations

KTH_EXPORT
kth_bool_t kth_chain_operation_from_string(kth_operation_mut_t self, char const* mnemonic);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_chain_operation_to_string(kth_operation_const_t self, kth_script_flags_t active_flags);


// Static utilities

KTH_EXPORT
kth_opcode_t kth_chain_operation_opcode_from_size(kth_size_t size);

KTH_EXPORT
kth_opcode_t kth_chain_operation_minimal_opcode_from_data(uint8_t const* data, kth_size_t n);

KTH_EXPORT
kth_opcode_t kth_chain_operation_nominal_opcode_from_data(uint8_t const* data, kth_size_t n);

KTH_EXPORT
kth_opcode_t kth_chain_operation_opcode_from_positive(uint8_t value);

KTH_EXPORT
uint8_t kth_chain_operation_opcode_to_positive(kth_opcode_t code);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_OPERATION_H_
