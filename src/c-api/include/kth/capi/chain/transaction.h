// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_CHAIN_TRANSACTION_H_
#define KTH_CAPI_CHAIN_TRANSACTION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_transaction_mut_t kth_chain_transaction_construct_default(void);

KTH_EXPORT
kth_transaction_mut_t kth_chain_transaction_construct(uint32_t version, uint32_t locktime, kth_input_list_const_t inputs, kth_output_list_const_t outputs);

KTH_EXPORT
void kth_chain_transaction_destruct(kth_transaction_mut_t transaction);

KTH_EXPORT
kth_transaction_mut_t kth_chain_transaction_copy(kth_transaction_const_t other);

KTH_EXPORT
kth_bool_t kth_chain_transaction_equal(kth_transaction_const_t a, kth_transaction_const_t b);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_valid(kth_transaction_const_t transaction);

KTH_EXPORT
uint32_t kth_chain_transaction_version(kth_transaction_const_t transaction);

KTH_EXPORT
uint32_t kth_chain_transaction_locktime(kth_transaction_const_t transaction);

KTH_EXPORT
kth_input_list_const_t kth_chain_transaction_inputs(kth_transaction_const_t transaction);

KTH_EXPORT
kth_output_list_const_t kth_chain_transaction_outputs(kth_transaction_const_t transaction);

KTH_EXPORT
kth_point_list_mut_t kth_chain_transaction_previous_outputs(kth_transaction_const_t transaction);

KTH_EXPORT
kth_point_list_mut_t kth_chain_transaction_missing_previous_outputs(kth_transaction_const_t transaction);

KTH_EXPORT
kth_hash_list_mut_t kth_chain_transaction_missing_previous_transactions(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_coinbase(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_null_non_coinbase(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_oversized_coinbase(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_mature(kth_transaction_const_t transaction, kth_size_t height);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_internal_double_spend(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_double_spend(kth_transaction_const_t transaction, kth_bool_t include_unconfirmed);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_dusty(kth_transaction_const_t transaction, uint64_t minimum_output_value);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_missing_previous_outputs(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_final(kth_transaction_const_t transaction, kth_size_t block_height, uint32_t block_time);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_locked(kth_transaction_const_t transaction, kth_size_t block_height, uint32_t median_time_past);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_locktime_conflict(kth_transaction_const_t transaction);

KTH_EXPORT
kth_size_t kth_chain_transaction_min_tx_size(kth_transaction_const_t transaction, kth_script_flags_t flags);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_standard(kth_transaction_const_t transaction);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_transaction_mut_t* out_result);

KTH_EXPORT
uint8_t const* kth_chain_transaction_to_data(kth_transaction_const_t transaction, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_transaction_serialized_size(kth_transaction_const_t transaction, kth_bool_t wire);

KTH_EXPORT
void kth_chain_transaction_set_version(kth_transaction_mut_t transaction, uint32_t value);

KTH_EXPORT
void kth_chain_transaction_set_locktime(kth_transaction_mut_t transaction, uint32_t value);

KTH_EXPORT
void kth_chain_transaction_set_inputs(kth_transaction_mut_t transaction, kth_input_list_const_t value);

KTH_EXPORT
void kth_chain_transaction_set_outputs(kth_transaction_mut_t transaction, kth_output_list_const_t value);

KTH_EXPORT
kth_hash_t kth_chain_transaction_outputs_hash(kth_transaction_const_t transaction);

KTH_EXPORT
kth_hash_t kth_chain_transaction_inpoints_hash(kth_transaction_const_t transaction);

KTH_EXPORT
kth_hash_t kth_chain_transaction_sequences_hash(kth_transaction_const_t transaction);

KTH_EXPORT
kth_hash_t kth_chain_transaction_utxos_hash(kth_transaction_const_t transaction);

KTH_EXPORT
kth_hash_t kth_chain_transaction_hash(kth_transaction_const_t transaction);

KTH_EXPORT
void kth_chain_transaction_hash_out(kth_transaction_const_t transaction, kth_hash_t* out_hash);

KTH_EXPORT
void kth_chain_transaction_recompute_hash(kth_transaction_mut_t transaction);

KTH_EXPORT
uint64_t kth_chain_transaction_fees(kth_transaction_const_t transaction);

KTH_EXPORT
uint64_t kth_chain_transaction_total_input_value(kth_transaction_const_t transaction);

KTH_EXPORT
uint64_t kth_chain_transaction_total_output_value(kth_transaction_const_t transaction);

KTH_EXPORT
kth_size_t kth_chain_transaction_signature_operations(kth_transaction_const_t transaction);

KTH_EXPORT
kth_bool_t kth_chain_transaction_is_overspent(kth_transaction_const_t transaction);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_check(kth_transaction_const_t transaction, kth_size_t max_block_size, kth_bool_t transaction_pool, kth_bool_t retarget);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_accept(kth_transaction_const_t transaction, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transaction_pool);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect(kth_transaction_const_t transaction);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect_ex(kth_transaction_const_t transaction, kth_chain_state_const_t state);

KTH_EXPORT
kth_error_code_t kth_chain_transaction_connect_input(kth_transaction_const_t transaction, kth_chain_state_const_t state, kth_size_t input_index);

KTH_EXPORT
void kth_chain_transaction_reset(kth_transaction_mut_t transaction);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_TRANSACTION_H_ */
