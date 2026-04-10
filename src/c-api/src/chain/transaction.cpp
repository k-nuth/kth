// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/transaction.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/transaction.hpp>

// Conversion functions
kth::domain::chain::transaction& kth_chain_transaction_mut_cpp(kth_transaction_mut_t o) {
    return *static_cast<kth::domain::chain::transaction*>(o);
}
kth::domain::chain::transaction const& kth_chain_transaction_const_cpp(kth_transaction_const_t o) {
    return *static_cast<kth::domain::chain::transaction const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_transaction_mut_t kth_chain_transaction_construct_default(void) {
    return new kth::domain::chain::transaction();
}

kth_error_code_t kth_chain_transaction_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_transaction_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, n));
    auto wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::transaction::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = new kth::domain::chain::transaction(std::move(*result));
    return kth_ec_success;
}

kth_transaction_mut_t kth_chain_transaction_construct_from_version_locktime_inputs_outputs(uint32_t version, uint32_t locktime, kth_input_list_const_t inputs, kth_output_list_const_t outputs) {
    KTH_PRECONDITION(inputs != nullptr);
    KTH_PRECONDITION(outputs != nullptr);
    auto const& inputs_cpp = kth_chain_input_list_const_cpp(inputs);
    auto const& outputs_cpp = kth_chain_output_list_const_cpp(outputs);
    return new kth::domain::chain::transaction(version, locktime, inputs_cpp, outputs_cpp);
}

kth_transaction_mut_t kth_chain_transaction_construct_from_transaction_hash(kth_transaction_const_t x, kth_hash_t hash) {
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth_chain_transaction_const_cpp(x);
    auto hash_cpp = kth::hash_to_cpp(hash.hash);
    return new kth::domain::chain::transaction(x_cpp, hash_cpp);
}

kth_transaction_mut_t kth_chain_transaction_construct_from_transaction_hash_unsafe(kth_transaction_const_t x, uint8_t const* hash) {
    KTH_PRECONDITION(x != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    auto const& x_cpp = kth_chain_transaction_const_cpp(x);
    auto hash_cpp = kth::hash_to_cpp(hash);
    return new kth::domain::chain::transaction(x_cpp, hash_cpp);
}


// Destructor

void kth_chain_transaction_destruct(kth_transaction_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_transaction_mut_cpp(self);
}


// Copy

kth_transaction_mut_t kth_chain_transaction_copy(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::transaction(kth_chain_transaction_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_transaction_equals(kth_transaction_const_t self, kth_transaction_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self) == kth_chain_transaction_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_transaction_to_data(kth_transaction_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto wire_cpp = kth::int_to_bool(wire);
    auto data = kth_chain_transaction_const_cpp(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_transaction_serialized_size(kth_transaction_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto wire_cpp = kth::int_to_bool(wire);
    return kth_chain_transaction_const_cpp(self).serialized_size(wire_cpp);
}


// Getters

kth_hash_t kth_chain_transaction_outputs_hash(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = kth_chain_transaction_const_cpp(self).outputs_hash();
    return kth::to_hash_t(value_cpp);
}

kth_hash_t kth_chain_transaction_inpoints_hash(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = kth_chain_transaction_const_cpp(self).inpoints_hash();
    return kth::to_hash_t(value_cpp);
}

kth_hash_t kth_chain_transaction_sequences_hash(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = kth_chain_transaction_const_cpp(self).sequences_hash();
    return kth::to_hash_t(value_cpp);
}

kth_hash_t kth_chain_transaction_utxos_hash(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = kth_chain_transaction_const_cpp(self).utxos_hash();
    return kth::to_hash_t(value_cpp);
}

kth_hash_t kth_chain_transaction_hash(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = kth_chain_transaction_const_cpp(self).hash();
    return kth::to_hash_t(value_cpp);
}

uint64_t kth_chain_transaction_fees(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).fees();
}

uint64_t kth_chain_transaction_total_input_value(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).total_input_value();
}

uint64_t kth_chain_transaction_total_output_value(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).total_output_value();
}

kth_size_t kth_chain_transaction_signature_operations_simple(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).signature_operations();
}

kth_error_code_t kth_chain_transaction_connect_simple(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_error_code_t>((kth_chain_transaction_const_cpp(self).connect()).value());
}

uint32_t kth_chain_transaction_version(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).version();
}

uint32_t kth_chain_transaction_locktime(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).locktime();
}

kth_input_list_const_t kth_chain_transaction_inputs(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_transaction_const_cpp(self).inputs());
}

kth_output_list_const_t kth_chain_transaction_outputs(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_transaction_const_cpp(self).outputs());
}

kth_point_list_mut_t kth_chain_transaction_previous_outputs(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<kth::domain::chain::point>(kth_chain_transaction_const_cpp(self).previous_outputs());
}

kth_point_list_mut_t kth_chain_transaction_missing_previous_outputs(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<kth::domain::chain::point>(kth_chain_transaction_const_cpp(self).missing_previous_outputs());
}

kth_hash_list_mut_t kth_chain_transaction_missing_previous_transactions(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<std::array<unsigned char, 32>>(kth_chain_transaction_const_cpp(self).missing_previous_transactions());
}


// Setters

void kth_chain_transaction_set_version(kth_transaction_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_transaction_mut_cpp(self).set_version(value);
}

void kth_chain_transaction_set_locktime(kth_transaction_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_transaction_mut_cpp(self).set_locktime(value);
}

void kth_chain_transaction_set_inputs(kth_transaction_mut_t self, kth_input_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_chain_input_list_const_cpp(value);
    kth_chain_transaction_mut_cpp(self).set_inputs(value_cpp);
}

void kth_chain_transaction_set_outputs(kth_transaction_mut_t self, kth_output_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_chain_output_list_const_cpp(value);
    kth_chain_transaction_mut_cpp(self).set_outputs(value_cpp);
}


// Predicates

kth_bool_t kth_chain_transaction_is_overspent(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_overspent());
}

kth_bool_t kth_chain_transaction_is_valid(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_valid());
}

kth_bool_t kth_chain_transaction_is_coinbase(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_coinbase());
}

kth_bool_t kth_chain_transaction_is_null_non_coinbase(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_null_non_coinbase());
}

kth_bool_t kth_chain_transaction_is_oversized_coinbase(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_oversized_coinbase());
}

kth_bool_t kth_chain_transaction_is_mature(kth_transaction_const_t self, kth_size_t height) {
    KTH_PRECONDITION(self != nullptr);
    auto height_cpp = static_cast<size_t>(height);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_mature(height_cpp));
}

kth_bool_t kth_chain_transaction_is_internal_double_spend(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_internal_double_spend());
}

kth_bool_t kth_chain_transaction_is_double_spend(kth_transaction_const_t self, kth_bool_t include_unconfirmed) {
    KTH_PRECONDITION(self != nullptr);
    auto include_unconfirmed_cpp = kth::int_to_bool(include_unconfirmed);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_double_spend(include_unconfirmed_cpp));
}

kth_bool_t kth_chain_transaction_is_dusty(kth_transaction_const_t self, uint64_t minimum_output_value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_dusty(minimum_output_value));
}

kth_bool_t kth_chain_transaction_is_missing_previous_outputs(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_missing_previous_outputs());
}

kth_bool_t kth_chain_transaction_is_final(kth_transaction_const_t self, kth_size_t block_height, uint32_t block_time) {
    KTH_PRECONDITION(self != nullptr);
    auto block_height_cpp = static_cast<size_t>(block_height);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_final(block_height_cpp, block_time));
}

kth_bool_t kth_chain_transaction_is_locked(kth_transaction_const_t self, kth_size_t block_height, uint32_t median_time_past) {
    KTH_PRECONDITION(self != nullptr);
    auto block_height_cpp = static_cast<size_t>(block_height);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_locked(block_height_cpp, median_time_past));
}

kth_bool_t kth_chain_transaction_is_locktime_conflict(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_locktime_conflict());
}

kth_bool_t kth_chain_transaction_is_standard(kth_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_transaction_const_cpp(self).is_standard());
}


// Operations

void kth_chain_transaction_recompute_hash(kth_transaction_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_transaction_mut_cpp(self).recompute_hash();
}

kth_error_code_t kth_chain_transaction_check(kth_transaction_const_t self, kth_size_t max_block_size, kth_bool_t transaction_pool, kth_bool_t retarget) {
    KTH_PRECONDITION(self != nullptr);
    auto max_block_size_cpp = static_cast<size_t>(max_block_size);
    auto transaction_pool_cpp = kth::int_to_bool(transaction_pool);
    auto retarget_cpp = kth::int_to_bool(retarget);
    return static_cast<kth_error_code_t>((kth_chain_transaction_const_cpp(self).check(max_block_size_cpp, transaction_pool_cpp, retarget_cpp)).value());
}

kth_error_code_t kth_chain_transaction_accept(kth_transaction_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transaction_pool) {
    KTH_PRECONDITION(self != nullptr);
    auto height_cpp = static_cast<size_t>(height);
    auto max_sigops_cpp = static_cast<size_t>(max_sigops);
    auto is_under_checkpoint_cpp = kth::int_to_bool(is_under_checkpoint);
    auto transaction_pool_cpp = kth::int_to_bool(transaction_pool);
    return static_cast<kth_error_code_t>((kth_chain_transaction_const_cpp(self).accept(flags, height_cpp, median_time_past, max_sigops_cpp, is_under_checkpoint_cpp, transaction_pool_cpp)).value());
}

kth_error_code_t kth_chain_transaction_connect(kth_transaction_const_t self, kth_chain_state_const_t state) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(state != nullptr);
    auto const& state_cpp = kth_chain_chain_state_const_cpp(state);
    return static_cast<kth_error_code_t>((kth_chain_transaction_const_cpp(self).connect(state_cpp)).value());
}

kth_error_code_t kth_chain_transaction_connect_input(kth_transaction_const_t self, kth_chain_state_const_t state, kth_size_t input_index) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(state != nullptr);
    auto const& state_cpp = kth_chain_chain_state_const_cpp(state);
    auto input_index_cpp = static_cast<size_t>(input_index);
    return static_cast<kth_error_code_t>((kth_chain_transaction_const_cpp(self).connect_input(state_cpp, input_index_cpp)).value());
}

void kth_chain_transaction_reset(kth_transaction_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_transaction_mut_cpp(self).reset();
}

kth_size_t kth_chain_transaction_signature_operations(kth_transaction_const_t self, kth_bool_t bip16, kth_bool_t bip141) {
    KTH_PRECONDITION(self != nullptr);
    auto bip16_cpp = kth::int_to_bool(bip16);
    auto bip141_cpp = kth::int_to_bool(bip141);
    return kth_chain_transaction_const_cpp(self).signature_operations(bip16_cpp, bip141_cpp);
}

kth_size_t kth_chain_transaction_min_tx_size(kth_transaction_const_t self, kth_script_flags_t flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_transaction_const_cpp(self).min_tx_size(flags);
}

} // extern "C"
