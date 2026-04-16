// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/block.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_block_mut_t kth_chain_block_construct_default(void) {
    return new kth::domain::chain::block();
}

kth_error_code_t kth_chain_block_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_block_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::block::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_block_mut_t kth_chain_block_construct(kth_header_const_t header, kth_transaction_list_const_t transactions) {
    KTH_PRECONDITION(header != nullptr);
    KTH_PRECONDITION(transactions != nullptr);
    auto const& header_cpp = kth::cpp_ref<kth::domain::chain::header>(header);
    auto const& transactions_cpp = kth::cpp_ref<kth::domain::chain::transaction::list>(transactions);
    return kth::make_leaked<kth::domain::chain::block>(header_cpp, transactions_cpp);
}


// Static factories

kth_block_mut_t kth_chain_block_genesis_mainnet(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_mainnet());
}

kth_block_mut_t kth_chain_block_genesis_testnet(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_testnet());
}

kth_block_mut_t kth_chain_block_genesis_regtest(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_regtest());
}

kth_block_mut_t kth_chain_block_genesis_testnet4(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_testnet4());
}

kth_block_mut_t kth_chain_block_genesis_scalenet(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_scalenet());
}

kth_block_mut_t kth_chain_block_genesis_chipnet(void) {
    return kth::make_leaked_if_valid(kth::domain::chain::block::genesis_chipnet());
}


// Destructor

void kth_chain_block_destruct(kth_block_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::chain::block>(self);
}


// Copy

kth_block_mut_t kth_chain_block_copy(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::block(kth::cpp_ref<kth::domain::chain::block>(self));
}


// Equality

kth_bool_t kth_chain_block_equals(kth_block_const_t self, kth_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self) == kth::cpp_ref<kth::domain::chain::block>(other));
}


// Serialization

uint8_t* kth_chain_block_to_data_simple(kth_block_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<kth::domain::chain::block>(self).to_data();
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_block_serialized_size(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::block>(self).serialized_size();
}

uint8_t* kth_chain_block_to_data(kth_block_const_t self, kth_size_t serialized_size, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const serialized_size_cpp = static_cast<size_t>(serialized_size);
    auto const data = kth::cpp_ref<kth::domain::chain::block>(self).to_data(serialized_size_cpp);
    return kth::create_c_array(data, *out_size);
}


// Getters

kth_size_t kth_chain_block_signature_operations_simple(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::block>(self).signature_operations();
}

kth_error_code_t kth_chain_block_check(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).check()).value());
}

kth_error_code_t kth_chain_block_connect(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).connect()).value());
}

kth_hash_list_mut_t kth_chain_block_to_hashes(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<std::array<unsigned char, 32>>(kth::cpp_ref<kth::domain::chain::block>(self).to_hashes());
}

kth_header_const_t kth_chain_block_header(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::chain::block>(self).header());
}

kth_transaction_list_const_t kth_chain_block_transactions(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::chain::block>(self).transactions());
}

kth_hash_t kth_chain_block_hash(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::chain::block>(self).hash());
}

uint64_t kth_chain_block_fees(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::block>(self).fees();
}

uint64_t kth_chain_block_claim(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::block>(self).claim();
}

kth_hash_t kth_chain_block_generate_merkle_root(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::chain::block>(self).generate_merkle_root());
}

kth_error_code_t kth_chain_block_check_transactions(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).check_transactions()).value());
}

kth_size_t kth_chain_block_non_coinbase_input_count(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::block>(self).non_coinbase_input_count();
}


// Setters

void kth_chain_block_set_transactions(kth_block_mut_t self, kth_transaction_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::transaction::list>(value);
    kth::cpp_ref<kth::domain::chain::block>(self).set_transactions(value_cpp);
}

void kth_chain_block_set_header(kth_block_mut_t self, kth_header_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::header>(value);
    kth::cpp_ref<kth::domain::chain::block>(self).set_header(value_cpp);
}


// Predicates

kth_bool_t kth_chain_block_is_valid(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_valid());
}

kth_bool_t kth_chain_block_is_extra_coinbases(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_extra_coinbases());
}

kth_bool_t kth_chain_block_is_final(kth_block_const_t self, kth_size_t height, uint32_t block_time) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_final(height_cpp, block_time));
}

kth_bool_t kth_chain_block_is_distinct_transaction_set(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_distinct_transaction_set());
}

kth_bool_t kth_chain_block_is_valid_coinbase_claim(kth_block_const_t self, kth_size_t height) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_valid_coinbase_claim(height_cpp));
}

kth_bool_t kth_chain_block_is_valid_coinbase_script(kth_block_const_t self, kth_size_t height) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_valid_coinbase_script(height_cpp));
}

kth_bool_t kth_chain_block_is_forward_reference(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_forward_reference());
}

kth_bool_t kth_chain_block_is_canonical_ordered(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_canonical_ordered());
}

kth_bool_t kth_chain_block_is_internal_double_spend(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_internal_double_spend());
}

kth_bool_t kth_chain_block_is_valid_merkle_root(kth_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::block>(self).is_valid_merkle_root());
}


// Operations

kth_size_t kth_chain_block_total_inputs(kth_block_const_t self, kth_bool_t with_coinbase) {
    KTH_PRECONDITION(self != nullptr);
    auto const with_coinbase_cpp = kth::int_to_bool(with_coinbase);
    return kth::cpp_ref<kth::domain::chain::block>(self).total_inputs(with_coinbase_cpp);
}

kth_error_code_t kth_chain_block_accept(kth_block_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_block_size_dynamic, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transactions) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    auto const max_block_size_dynamic_cpp = static_cast<size_t>(max_block_size_dynamic);
    auto const max_sigops_cpp = static_cast<size_t>(max_sigops);
    auto const is_under_checkpoint_cpp = kth::int_to_bool(is_under_checkpoint);
    auto const transactions_cpp = kth::int_to_bool(transactions);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).accept(flags, height_cpp, median_time_past, max_block_size_dynamic_cpp, max_sigops_cpp, is_under_checkpoint_cpp, transactions_cpp)).value());
}

uint64_t kth_chain_block_reward(kth_block_const_t self, kth_size_t height) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    return kth::cpp_ref<kth::domain::chain::block>(self).reward(height_cpp);
}

kth_size_t kth_chain_block_signature_operations(kth_block_const_t self, kth_bool_t bip16, kth_bool_t bip141) {
    KTH_PRECONDITION(self != nullptr);
    auto const bip16_cpp = kth::int_to_bool(bip16);
    auto const bip141_cpp = kth::int_to_bool(bip141);
    return kth::cpp_ref<kth::domain::chain::block>(self).signature_operations(bip16_cpp, bip141_cpp);
}

kth_error_code_t kth_chain_block_accept_transactions(kth_block_const_t self, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = static_cast<size_t>(height);
    auto const max_sigops_cpp = static_cast<size_t>(max_sigops);
    auto const is_under_checkpoint_cpp = kth::int_to_bool(is_under_checkpoint);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).accept_transactions(flags, height_cpp, median_time_past, max_sigops_cpp, is_under_checkpoint_cpp)).value());
}

kth_error_code_t kth_chain_block_connect_transactions(kth_block_const_t self, kth_chain_state_const_t state) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(state != nullptr);
    auto const& state_cpp = kth::cpp_ref<kth::domain::chain::chain_state>(state);
    return static_cast<kth_error_code_t>((kth::cpp_ref<kth::domain::chain::block>(self).connect_transactions(state_cpp)).value());
}

void kth_chain_block_reset(kth_block_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::chain::block>(self).reset();
}


// Static utilities

kth_size_t kth_chain_block_locator_size(kth_size_t top) {
    auto const top_cpp = static_cast<size_t>(top);
    return kth::domain::chain::block::locator_size(top_cpp);
}

kth_u64_list_mut_t kth_chain_block_locator_heights(kth_size_t top) {
    auto const top_cpp = static_cast<size_t>(top);
    auto const cpp_result = kth::domain::chain::block::locator_heights(top_cpp);
    return new std::vector<uint64_t>(cpp_result.begin(), cpp_result.end());
}

uint64_t kth_chain_block_subsidy(kth_size_t height, kth_bool_t retarget) {
    auto const height_cpp = static_cast<size_t>(height);
    auto const retarget_cpp = kth::int_to_bool(retarget);
    return kth::domain::chain::block::subsidy(height_cpp, retarget_cpp);
}

} // extern "C"
