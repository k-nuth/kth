// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/transaction.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Conversion functions
kth::domain::chain::transaction& kth_chain_transaction_cpp(kth_transaction_mut_t o) {
    return *static_cast<kth::domain::chain::transaction*>(o);
}
kth::domain::chain::transaction const& kth_chain_transaction_const_cpp(kth_transaction_const_t o) {
    return *static_cast<kth::domain::chain::transaction const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_transaction_mut_t kth_chain_transaction_construct_default() {
    return new kth::domain::chain::transaction();
}

kth_transaction_mut_t kth_chain_transaction_construct(uint32_t version, uint32_t locktime, kth_input_list_const_t inputs, kth_output_list_const_t outputs) {
    auto const& inputs_cpp = kth_chain_input_list_const_cpp(inputs);
    auto const& outputs_cpp = kth_chain_output_list_const_cpp(outputs);
    return new kth::domain::chain::transaction(version, locktime, inputs_cpp, outputs_cpp);
}

void kth_chain_transaction_destruct(kth_transaction_mut_t transaction) {
    if (transaction == nullptr) return;
    delete &kth_chain_transaction_cpp(transaction);
}

kth_transaction_mut_t kth_chain_transaction_copy(kth_transaction_const_t other) {
    return new kth::domain::chain::transaction(kth_chain_transaction_const_cpp(other));
}

kth_bool_t kth_chain_transaction_equal(kth_transaction_const_t a, kth_transaction_const_t b) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(a) == kth_chain_transaction_const_cpp(b));
}

kth_bool_t kth_chain_transaction_is_valid(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_valid());
}

uint32_t kth_chain_transaction_version(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).version();
}

uint32_t kth_chain_transaction_locktime(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).locktime();
}

kth_input_list_const_t kth_chain_transaction_inputs(kth_transaction_const_t transaction) {
    return &kth_chain_transaction_const_cpp(transaction).inputs();
}

kth_output_list_const_t kth_chain_transaction_outputs(kth_transaction_const_t transaction) {
    return &kth_chain_transaction_const_cpp(transaction).outputs();
}

kth_point_list_mut_t kth_chain_transaction_previous_outputs(kth_transaction_const_t transaction) {
    return kth::move_or_copy_and_leak(kth_chain_transaction_const_cpp(transaction).previous_outputs());
}

kth_point_list_mut_t kth_chain_transaction_missing_previous_outputs(kth_transaction_const_t transaction) {
    return kth::move_or_copy_and_leak(kth_chain_transaction_const_cpp(transaction).missing_previous_outputs());
}

kth_hash_list_mut_t kth_chain_transaction_missing_previous_transactions(kth_transaction_const_t transaction) {
    return kth::move_or_copy_and_leak(kth_chain_transaction_const_cpp(transaction).missing_previous_transactions());
}

kth_bool_t kth_chain_transaction_is_coinbase(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_coinbase());
}

kth_bool_t kth_chain_transaction_is_null_non_coinbase(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_null_non_coinbase());
}

kth_bool_t kth_chain_transaction_is_oversized_coinbase(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_oversized_coinbase());
}

kth_bool_t kth_chain_transaction_is_mature(kth_transaction_const_t transaction, kth_size_t height) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_mature(height));
}

kth_bool_t kth_chain_transaction_is_internal_double_spend(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_internal_double_spend());
}

kth_bool_t kth_chain_transaction_is_double_spend(kth_transaction_const_t transaction, kth_bool_t include_unconfirmed) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_double_spend(kth::int_to_bool(include_unconfirmed)));
}

kth_bool_t kth_chain_transaction_is_dusty(kth_transaction_const_t transaction, uint64_t minimum_output_value) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_dusty(minimum_output_value));
}

kth_bool_t kth_chain_transaction_is_missing_previous_outputs(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_missing_previous_outputs());
}

kth_bool_t kth_chain_transaction_is_final(kth_transaction_const_t transaction, kth_size_t block_height, uint32_t block_time) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_final(block_height, block_time));
}

kth_bool_t kth_chain_transaction_is_locked(kth_transaction_const_t transaction, kth_size_t block_height, uint32_t median_time_past) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_locked(block_height, median_time_past));
}

kth_bool_t kth_chain_transaction_is_locktime_conflict(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_locktime_conflict());
}

kth_size_t kth_chain_transaction_min_tx_size(kth_transaction_const_t transaction, kth_script_flags_t flags) {
    return kth_chain_transaction_const_cpp(transaction).min_tx_size(flags);
}

kth_bool_t kth_chain_transaction_is_standard(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_standard());
}

kth_error_code_t kth_chain_transaction_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_transaction_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::transaction::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

uint8_t const* kth_chain_transaction_to_data(kth_transaction_const_t transaction, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_transaction_const_cpp(transaction).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_transaction_serialized_size(kth_transaction_const_t transaction, kth_bool_t wire) {
    return kth_chain_transaction_const_cpp(transaction).serialized_size(kth::int_to_bool(wire));
}

void kth_chain_transaction_set_version(kth_transaction_mut_t transaction, uint32_t value) {
    kth_chain_transaction_cpp(transaction).set_version(value);
}

void kth_chain_transaction_set_locktime(kth_transaction_mut_t transaction, uint32_t value) {
    kth_chain_transaction_cpp(transaction).set_locktime(value);
}

void kth_chain_transaction_set_inputs(kth_transaction_mut_t transaction, kth_input_list_const_t value) {
    kth_chain_transaction_cpp(transaction).set_inputs(kth_chain_input_list_const_cpp(value));
}

void kth_chain_transaction_set_outputs(kth_transaction_mut_t transaction, kth_output_list_const_t value) {
    kth_chain_transaction_cpp(transaction).set_outputs(kth_chain_output_list_const_cpp(value));
}

kth_hash_t kth_chain_transaction_outputs_hash(kth_transaction_const_t transaction) {
    return kth::to_hash_t(kth_chain_transaction_const_cpp(transaction).outputs_hash());
}

kth_hash_t kth_chain_transaction_inpoints_hash(kth_transaction_const_t transaction) {
    return kth::to_hash_t(kth_chain_transaction_const_cpp(transaction).inpoints_hash());
}

kth_hash_t kth_chain_transaction_sequences_hash(kth_transaction_const_t transaction) {
    return kth::to_hash_t(kth_chain_transaction_const_cpp(transaction).sequences_hash());
}

kth_hash_t kth_chain_transaction_utxos_hash(kth_transaction_const_t transaction) {
    return kth::to_hash_t(kth_chain_transaction_const_cpp(transaction).utxos_hash());
}

kth_hash_t kth_chain_transaction_hash(kth_transaction_const_t transaction) {
    auto hash_cpp = kth_chain_transaction_const_cpp(transaction).hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_transaction_hash_out(kth_transaction_const_t transaction, kth_hash_t* out_hash) {
    auto hash_cpp = kth_chain_transaction_const_cpp(transaction).hash();
    kth::copy_c_hash(hash_cpp, out_hash);
}

void kth_chain_transaction_recompute_hash(kth_transaction_mut_t transaction) {
    kth_chain_transaction_cpp(transaction).recompute_hash();
}

uint64_t kth_chain_transaction_fees(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).fees();
}

uint64_t kth_chain_transaction_total_input_value(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).total_input_value();
}

uint64_t kth_chain_transaction_total_output_value(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).total_output_value();
}

kth_size_t kth_chain_transaction_signature_operations(kth_transaction_const_t transaction) {
    return kth_chain_transaction_const_cpp(transaction).signature_operations();
}

kth_bool_t kth_chain_transaction_is_overspent(kth_transaction_const_t transaction) {
    return kth::bool_to_int(kth_chain_transaction_const_cpp(transaction).is_overspent());
}

kth_error_code_t kth_chain_transaction_check(kth_transaction_const_t transaction, kth_size_t max_block_size, kth_bool_t transaction_pool, kth_bool_t retarget) {
    return kth::to_c_err(kth_chain_transaction_const_cpp(transaction).check(max_block_size, kth::int_to_bool(transaction_pool), kth::int_to_bool(retarget)));
}

kth_error_code_t kth_chain_transaction_accept(kth_transaction_const_t transaction, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transaction_pool) {
    return kth::to_c_err(kth_chain_transaction_const_cpp(transaction).accept(flags, height, median_time_past, max_sigops, kth::int_to_bool(is_under_checkpoint), kth::int_to_bool(transaction_pool)));
}

kth_error_code_t kth_chain_transaction_connect(kth_transaction_const_t transaction) {
    return kth::to_c_err(kth_chain_transaction_const_cpp(transaction).connect());
}

kth_error_code_t kth_chain_transaction_connect_ex(kth_transaction_const_t transaction, kth_chain_state_const_t state) {
    return kth::to_c_err(kth_chain_transaction_const_cpp(transaction).connect(kth_chain_chain_state_const_cpp(state)));
}

kth_error_code_t kth_chain_transaction_connect_input(kth_transaction_const_t transaction, kth_chain_state_const_t state, kth_size_t input_index) {
    return kth::to_c_err(kth_chain_transaction_const_cpp(transaction).connect_input(kth_chain_chain_state_const_cpp(state), input_index));
}

void kth_chain_transaction_reset(kth_transaction_mut_t transaction) {
    kth_chain_transaction_cpp(transaction).reset();
}

} // extern "C"
