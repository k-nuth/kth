// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_CHAIN_OUTPUT_H_
#define KTH_CAPI_CHAIN_OUTPUT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/token_capability.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct_default(void);

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct(uint64_t value, kth_script_const_t script);

KTH_EXPORT
void kth_chain_output_destruct(kth_output_mut_t output);

KTH_EXPORT
kth_output_mut_t kth_chain_output_copy(kth_output_const_t other);

KTH_EXPORT
kth_bool_t kth_chain_output_is_valid(kth_output_const_t output);

KTH_EXPORT
uint64_t kth_chain_output_value(kth_output_const_t output);

KTH_EXPORT
void kth_chain_output_set_value(kth_output_mut_t output, uint64_t value);

KTH_EXPORT
kth_script_const_t kth_chain_output_script(kth_output_const_t output);

KTH_EXPORT
kth_token_data_const_t kth_chain_output_token_data(kth_output_const_t output);

KTH_EXPORT
void kth_chain_output_set_token_data(kth_output_mut_t output, kth_token_data_const_t value);

KTH_EXPORT
kth_size_t kth_chain_output_signature_operations(kth_output_const_t output, kth_bool_t bip141);

KTH_EXPORT
kth_bool_t kth_chain_output_is_dust(kth_output_const_t output, uint64_t minimum_output_value);

KTH_EXPORT
void kth_chain_output_reset(kth_output_mut_t output);

KTH_EXPORT
kth_error_code_t kth_chain_output_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_output_mut_t* out_result);

KTH_EXPORT
uint8_t const* kth_chain_output_to_data(kth_output_const_t output, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_output_serialized_size(kth_output_const_t output, kth_bool_t wire);

KTH_EXPORT
void kth_chain_output_set_script(kth_output_mut_t output, kth_script_const_t value);

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct_with_token_fungible(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, int64_t token_amount);

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct_with_token_non_fungible(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, kth_token_capability_t capability, uint8_t const* commitment_data, kth_size_t commitment_n);

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct_with_token_both(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, int64_t token_amount, kth_token_capability_t capability, uint8_t const* commitment_data, kth_size_t commitment_n);

KTH_EXPORT
kth_output_mut_t kth_chain_output_construct_with_token_data(uint64_t value, kth_script_const_t script, kth_token_data_const_t token_data);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_OUTPUT_H_ */
