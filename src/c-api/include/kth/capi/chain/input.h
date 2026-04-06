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

KTH_EXPORT
kth_input_mut_t kth_chain_input_construct_default(void);

KTH_EXPORT
kth_input_mut_t kth_chain_input_construct(kth_output_point_const_t previous_output, kth_script_const_t script, uint32_t sequence);

KTH_EXPORT
void kth_chain_input_destruct(kth_input_mut_t input);

KTH_EXPORT
kth_input_mut_t kth_chain_input_copy(kth_input_const_t other);

KTH_EXPORT
kth_bool_t kth_chain_input_is_valid(kth_input_const_t input);

KTH_EXPORT
uint8_t const* kth_chain_input_to_data(kth_input_const_t input, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_input_serialized_size(kth_input_const_t input, kth_bool_t wire);

KTH_EXPORT
kth_output_point_const_t kth_chain_input_previous_output(kth_input_const_t input);

KTH_EXPORT
void kth_chain_input_set_previous_output(kth_input_mut_t input, kth_output_point_const_t value);

KTH_EXPORT
kth_script_const_t kth_chain_input_script(kth_input_const_t input);

KTH_EXPORT
uint32_t kth_chain_input_sequence(kth_input_const_t input);

KTH_EXPORT
void kth_chain_input_set_sequence(kth_input_mut_t input, uint32_t value);

KTH_EXPORT
kth_bool_t kth_chain_input_is_final(kth_input_const_t input);

KTH_EXPORT
kth_bool_t kth_chain_input_is_locked(kth_input_const_t input, kth_size_t block_height, uint32_t median_time_past);

KTH_EXPORT
kth_size_t kth_chain_input_signature_operations(kth_input_const_t input, kth_bool_t bip16, kth_bool_t bip141);

KTH_EXPORT
kth_error_code_t kth_chain_input_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_input_mut_t* out_result);

KTH_EXPORT
void kth_chain_input_set_script(kth_input_mut_t input, kth_script_const_t value);

KTH_EXPORT
void kth_chain_input_reset(kth_input_mut_t input);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_INPUT_H_ */
