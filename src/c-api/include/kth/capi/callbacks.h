// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CALLBACKS_H_
#define KTH_CAPI_CALLBACKS_H_

#include <stdint.h>

#include <kth/capi/error.h>
#include <kth/capi/handles.h>

#ifdef __cplusplus
extern "C" {
#endif

// VM
// ----------------------------------------------------------------------------
// `interpreter::debug_step_until` predicate: fires on each post-step
// snapshot. Return non-zero to stop stepping, zero to continue. The
// snapshot handle is borrowed (do not destruct); `user_data` is the
// pointer the caller passed alongside the predicate.
//
// MUST NOT throw (when compiled as C++): the C++ runtime does not
// propagate exceptions across the `extern "C"` boundary, and an
// exception thrown by this callback would unwind through
// `interpreter::debug_step_until` — undefined behaviour. Convert
// exceptional conditions in the predicate body into a "stop" signal
// (return non-zero, stash the diagnostic in `user_data`) instead.
typedef kth_bool_t (*kth_debug_step_predicate_t)(kth_debug_snapshot_const_t snapshot, void* user_data);

// Async safe_chain handler signatures
// ----------------------------------------------------------------------------
typedef void (*kth_run_handler_t)(kth_node_t, void*, kth_error_code_t);
typedef void (*kth_stealth_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_stealth_compact_list_mut_t);

// Owned: `block` (`kth_block_mut_t`) — caller must release with `kth_chain_block_destruct`.
typedef void (*kth_block_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_block_mut_t block, kth_size_t);

// Owned: `header` (`kth_header_mut_t`) — caller must release with `kth_chain_header_destruct`.
// Owned: `tx_hashes` (`kth_hash_list_mut_t`) — caller must release with `kth_core_hash_list_destruct`.
typedef void (*kth_block_header_txs_size_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_header_mut_t header, kth_size_t, kth_hash_list_mut_t tx_hashes, uint64_t);
typedef void (*kth_blockhash_timestamp_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_hash_t, uint32_t, kth_size_t);
typedef void (*kth_block_height_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_size_t);

// Owned: `header` (`kth_header_mut_t`) — caller must release with `kth_chain_header_destruct`.
typedef void (*kth_block_header_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_header_mut_t header, kth_size_t);

// Owned: `block` (`kth_compact_block_mut_t`) — caller must release with `kth_chain_compact_block_destruct`.
typedef void (*kth_compact_block_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_compact_block_mut_t block, kth_size_t);

// Owned: `history` (`kth_history_compact_list_mut_t`) — caller must release with `kth_chain_history_compact_list_destruct`.
typedef void (*kth_history_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_history_compact_list_mut_t history);
typedef void (*kth_last_height_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_size_t);

// Owned: `block` (`kth_merkle_block_mut_t`) — caller must release with `kth_chain_merkle_block_destruct`.
typedef void (*kth_merkle_block_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_merkle_block_mut_t block, kth_size_t);
typedef void (*kth_output_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_output_mut_t output);

// Owned: `spend` (`kth_input_point_mut_t`) — caller must release with `kth_chain_input_point_destruct`.
typedef void (*kth_spend_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_input_point_mut_t spend);

// Owned: `tx` (`kth_transaction_mut_t`) — caller must release with `kth_chain_transaction_destruct`.
typedef void (*kth_transaction_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_transaction_mut_t tx, kth_size_t, kth_size_t);
typedef void (*kth_transaction_index_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_size_t, kth_size_t);
typedef void (*kth_validate_tx_handler_t)(kth_chain_t, void*, kth_error_code_t, char const*);
typedef void (*kth_block_locator_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_get_headers_mut_t);
typedef void (*kth_result_handler_t)(kth_chain_t, void*, kth_error_code_t);

// Owned: `tx_hashes` (`kth_hash_list_mut_t`) — caller must release with `kth_core_hash_list_destruct`.
typedef void (*kth_transactions_by_address_fetch_handler_t)(kth_chain_t, void*, kth_error_code_t, kth_hash_list_mut_t tx_hashes);

// Owned: `incoming` (`kth_block_list_mut_t`) — caller must release with `kth_chain_block_list_destruct`.
// Owned: `replaced` (`kth_block_list_mut_t`) — caller must release with `kth_chain_block_list_destruct`.
typedef kth_bool_t (*kth_subscribe_blockchain_handler_t)(kth_node_t, kth_chain_t, void*, kth_error_code_t, kth_size_t, kth_block_list_mut_t incoming, kth_block_list_mut_t replaced);

// Owned: `tx` (`kth_transaction_mut_t`) — caller must release with `kth_chain_transaction_destruct`.
typedef kth_bool_t (*kth_subscribe_transaction_handler_t)(kth_node_t, kth_chain_t, void*, kth_error_code_t, kth_transaction_mut_t tx);

// Owned: `ds_proof` (`kth_double_spend_proof_mut_t`) — caller must release with `kth_chain_double_spend_proof_destruct`.
typedef kth_bool_t (*kth_subscribe_ds_proof_handler_t)(kth_node_t, kth_chain_t, void*, kth_error_code_t, kth_double_spend_proof_mut_t ds_proof);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CALLBACKS_H_ */
