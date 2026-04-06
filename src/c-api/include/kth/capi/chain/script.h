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

KTH_EXPORT
kth_script_mut_t kth_chain_script_construct_default(void);

KTH_EXPORT
void kth_chain_script_destruct(kth_script_mut_t script);

KTH_EXPORT
kth_script_mut_t kth_chain_script_copy(kth_script_const_t other);

KTH_EXPORT
kth_bool_t kth_chain_script_equal(kth_script_const_t a, kth_script_const_t b);

KTH_EXPORT
kth_bool_t kth_chain_script_is_valid(kth_script_const_t script);

KTH_EXPORT
uint8_t const* kth_chain_script_to_data(kth_script_const_t script, kth_bool_t prefix, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_script_serialized_size(kth_script_const_t script, kth_bool_t prefix);

KTH_EXPORT
uint8_t const* kth_chain_script_bytes(kth_script_const_t script, kth_size_t* out_size);

KTH_EXPORT
kth_error_code_t kth_chain_script_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_script_mut_t* out_result);

KTH_EXPORT
kth_error_code_t kth_chain_script_from_data_with_size(uint8_t const* data, kth_size_t n, kth_size_t size, kth_script_mut_t* out_result);

KTH_EXPORT
kth_bool_t kth_chain_script_is_valid_operations(kth_script_const_t script);

KTH_EXPORT
char const* kth_chain_script_to_string(kth_script_const_t script, kth_script_flags_t active_flags);

KTH_EXPORT
void kth_chain_script_clear(kth_script_mut_t script);

KTH_EXPORT
kth_bool_t kth_chain_script_empty(kth_script_const_t script);

KTH_EXPORT
kth_size_t kth_chain_script_size(kth_script_const_t script);

KTH_EXPORT
kth_operation_list_const_t kth_chain_script_operations(kth_script_const_t script);

KTH_EXPORT
kth_operation_mut_t kth_chain_script_first_operation(kth_script_const_t script);

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
kth_script_pattern_t kth_chain_script_pattern(kth_script_const_t script);

KTH_EXPORT
kth_script_pattern_t kth_chain_script_output_pattern(kth_script_const_t script);

KTH_EXPORT
kth_script_pattern_t kth_chain_script_input_pattern(kth_script_const_t script);

KTH_EXPORT
kth_size_t kth_chain_script_sigops(kth_script_const_t script, kth_bool_t accurate);

KTH_EXPORT
kth_bool_t kth_chain_script_is_unspendable(kth_script_const_t script);

KTH_EXPORT
void kth_chain_script_reset(kth_script_mut_t script);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_to_script_hash(kth_script_const_t script, kth_script_flags_t flags);

KTH_EXPORT
kth_bool_t kth_chain_script_is_pay_to_script_hash_32(kth_script_const_t script, kth_script_flags_t flags);

// Auto-generation warnings:
// SKIP to_null_data_pattern(byte_span): unsupported param type 'byte_span'
// SKIP to_pay_public_key_pattern(byte_span): unsupported param type 'byte_span'
// SKIP to_pay_multisig_pattern(uint8_t, point_list const&): unsupported param type 'uint8_t'
// SKIP to_pay_multisig_pattern(uint8_t, data_stack const&): unsupported param type 'uint8_t'

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_SCRIPT_H_ */
