// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_SCRIPT_H_
#define KTH_CAPI_CHAIN_SCRIPT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>
#include <kth/capi/chain/script_pattern.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_script_mut_t`. Caller must release with `kth_chain_script_destruct`. */
KTH_EXPORT KTH_OWNED
kth_script_mut_t kth_chain_script_construct_default(void);

/** @param[out] out Must point to a null `kth_script_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_script_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_script_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_script_mut_t* out);

/** @return Owned `kth_script_mut_t`. Caller must release with `kth_chain_script_destruct`. */
KTH_EXPORT KTH_OWNED
kth_script_mut_t kth_chain_script_construct_from_operations(kth_operation_list_const_t ops);

/** @return Owned `kth_script_mut_t`. Caller must release with `kth_chain_script_destruct`. */
KTH_EXPORT KTH_OWNED
kth_script_mut_t kth_chain_script_construct_from_encoded_prefix(uint8_t const* encoded, kth_size_t encoded_n, kth_bool_t prefix);


// Destructor

KTH_EXPORT
void kth_chain_script_destruct(kth_script_mut_t self);


// Copy

/** @return Owned `kth_script_mut_t`. Caller must release with `kth_chain_script_destruct`. */
KTH_EXPORT KTH_OWNED
kth_script_mut_t kth_chain_script_copy(kth_script_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_chain_script_equals(kth_script_const_t self, kth_script_const_t other);


// Serialization

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_script_to_data(kth_script_const_t self, kth_bool_t prefix, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_script_serialized_size(kth_script_const_t self, kth_bool_t prefix);


// Getters

KTH_EXPORT
kth_bool_t kth_chain_script_empty(kth_script_const_t self);

KTH_EXPORT
kth_size_t kth_chain_script_size(kth_script_const_t self);

KTH_EXPORT
kth_operation_const_t kth_chain_script_front(kth_script_const_t self);

KTH_EXPORT
kth_operation_const_t kth_chain_script_back(kth_script_const_t self);

KTH_EXPORT
kth_operation_list_const_t kth_chain_script_operations(kth_script_const_t self);

/** @return Owned `kth_operation_mut_t`. Caller must release with `kth_chain_operation_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_mut_t kth_chain_script_first_operation(kth_script_const_t self);

KTH_EXPORT
kth_script_pattern_t kth_chain_script_pattern(kth_script_const_t self);

KTH_EXPORT
kth_script_pattern_t kth_chain_script_output_pattern(kth_script_const_t self);

KTH_EXPORT
kth_script_pattern_t kth_chain_script_input_pattern(kth_script_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_chain_script_bytes(kth_script_const_t self, kth_size_t* out_size);


// Predicates

KTH_EXPORT
kth_bool_t kth_chain_script_is_valid_operations(kth_script_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_script_is_push_only(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_relaxed_push(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_coinbase_pattern(kth_operation_list_const_t ops, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_script_is_null_data_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_multisig_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_public_key_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_public_key_hash_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_script_hash_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_script_hash_32_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_sign_multisig_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_sign_public_key_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_sign_public_key_hash_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_sign_script_hash_pattern(kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_is_unspendable(kth_script_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_to_script_hash(kth_script_const_t self, kth_script_flags_t flags);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_to_script_hash_32(kth_script_const_t self, kth_script_flags_t flags);

KTH_EXPORT
kth_bool_t kth_chain_script_is_valid(kth_script_const_t self);

KTH_EXPORT
kth_bool_t kth_chain_script_is_enabled(kth_script_flags_t active_flags, kth_script_flags_t fork);


// Operations

KTH_EXPORT
void kth_chain_script_from_operations(kth_script_mut_t self, kth_operation_list_const_t ops);

KTH_EXPORT
kth_bool_t kth_chain_script_from_string(kth_script_mut_t self, char const* mnemonic);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_chain_script_to_string(kth_script_const_t self, kth_script_flags_t active_flags);

KTH_EXPORT
void kth_chain_script_clear(kth_script_mut_t self);

KTH_EXPORT
kth_size_t kth_chain_script_sigops(kth_script_const_t self, kth_bool_t accurate);

KTH_EXPORT
void kth_chain_script_reset(kth_script_mut_t self);


// Static utilities

/** @param[out] out Must point to a null `kth_script_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_script_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_chain_script_from_data_with_size(uint8_t const* data, kth_size_t n, kth_size_t size, KTH_OUT_OWNED kth_script_mut_t* out);

KTH_EXPORT
kth_hash_t kth_chain_script_generate_signature_hash(kth_transaction_const_t tx, uint32_t input_index, kth_script_const_t script_code, uint8_t sighash_type, kth_script_flags_t active_flags, uint64_t value, kth_size_t* out_size);

KTH_EXPORT
kth_bool_t kth_chain_script_check_signature(uint8_t const* signature, uint8_t sighash_type, uint8_t const* public_key, kth_size_t public_key_n, kth_script_const_t script_code, kth_transaction_const_t tx, uint32_t input_index, kth_script_flags_t active_flags, uint64_t value, kth_size_t* out_size);

KTH_EXPORT
kth_error_code_t kth_chain_script_create_endorsement(uint8_t const* secret, kth_script_const_t prevout_script, kth_transaction_const_t tx, uint32_t input_index, uint8_t sighash_type, kth_script_flags_t active_flags, uint64_t value, kth_endorsement_type_t type, KTH_OUT_OWNED uint8_t** out, kth_size_t* out_size);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_null_data_pattern(uint8_t const* data, kth_size_t data_n);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_public_key_pattern(uint8_t const* point, kth_size_t point_n);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern(uint8_t const* hash);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern_unlocking_placeholder(kth_size_t endorsement_size, kth_size_t pubkey_size);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_pattern_unlocking_placeholder(kth_size_t script_size, kth_bool_t multisig);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_pattern(uint8_t const* hash);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_32_pattern(uint8_t const* hash);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_multisig_pattern_ec_compressed_list(uint8_t signatures, kth_ec_compressed_list_const_t points);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_chain_script_to_pay_multisig_pattern_data_stack(uint8_t signatures, kth_data_stack_const_t points);

KTH_EXPORT
kth_error_code_t kth_chain_script_verify(kth_transaction_const_t tx, uint32_t input_index, kth_script_flags_t flags, kth_script_const_t input_script, kth_script_const_t prevout_script, uint64_t arg5);

KTH_EXPORT
kth_error_code_t kth_chain_script_verify_simple(kth_transaction_const_t tx, uint32_t input, kth_script_flags_t flags);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_SCRIPT_H_
