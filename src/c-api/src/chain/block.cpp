// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/transaction.hpp>

// Conversion functions
kth::domain::chain::block& kth_chain_block_cpp(kth_block_mut_t o) {
    return *static_cast<kth::domain::chain::block*>(o);
}
kth::domain::chain::block const& kth_chain_block_const_cpp(kth_block_const_t o) {
    return *static_cast<kth::domain::chain::block const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_block_mut_t kth_chain_block_construct_default() {
    return new kth::domain::chain::block();
}

kth_block_mut_t kth_chain_block_construct(kth_header_const_t header, kth_transaction_list_const_t transactions) {
    auto const& header_cpp = kth_chain_header_const_cpp(header);
    auto const& transactions_cpp = kth_chain_transaction_list_const_cpp(transactions);
    return new kth::domain::chain::block(header_cpp, transactions_cpp);
}

void kth_chain_block_destruct(kth_block_mut_t block) {
    if (block == nullptr) return;
    delete &kth_chain_block_cpp(block);
}

kth_block_mut_t kth_chain_block_copy(kth_block_const_t other) {
    return new kth::domain::chain::block(kth_chain_block_const_cpp(other));
}

kth_bool_t kth_chain_block_equal(kth_block_const_t a, kth_block_const_t b) {
    return kth::bool_to_int(kth_chain_block_const_cpp(a) == kth_chain_block_const_cpp(b));
}

kth_bool_t kth_chain_block_is_valid(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_valid());
}

kth_hash_list_mut_t kth_chain_block_to_hashes(kth_block_const_t block) {
    return kth::move_or_copy_and_leak(kth_chain_block_const_cpp(block).to_hashes());
}

kth_header_const_t kth_chain_block_header(kth_block_const_t block) {
    return &kth_chain_block_const_cpp(block).header();
}

void kth_chain_block_set_header(kth_block_mut_t block, kth_header_const_t value) {
    kth_chain_block_cpp(block).set_header(kth_chain_header_const_cpp(value));
}

kth_transaction_list_const_t kth_chain_block_transactions(kth_block_const_t block) {
    auto const& obj_cpp = kth_chain_block_const_cpp(block);
    return kth_chain_transaction_list_construct_from_cpp(obj_cpp.transactions());
}

kth_hash_t kth_chain_block_hash(kth_block_const_t block) {
    auto hash_cpp = kth_chain_block_const_cpp(block).hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_block_hash_out(kth_block_const_t block, kth_hash_t* out_hash) {
    auto hash_cpp = kth_chain_block_const_cpp(block).hash();
    kth::copy_c_hash(hash_cpp, out_hash);
}

/*static*/
uint64_t kth_chain_block_subsidy(kth_size_t height) {
    return kth::domain::chain::block::subsidy(height);
}

uint64_t kth_chain_block_fees(kth_block_const_t block) {
    return kth_chain_block_const_cpp(block).fees();
}

uint64_t kth_chain_block_claim(kth_block_const_t block) {
    return kth_chain_block_const_cpp(block).claim();
}

uint64_t kth_chain_block_reward(kth_block_const_t block, kth_size_t height) {
    return kth_chain_block_const_cpp(block).reward(height);
}

char const* kth_chain_block_proof_str(kth_block_const_t block) {
    auto proof_str = kth_chain_block_const_cpp(block).proof().str();
    return kth::create_c_str(proof_str);
}

kth_hash_t kth_chain_block_generate_merkle_root(kth_block_const_t block) {
    auto hash_cpp = kth_chain_block_const_cpp(block).generate_merkle_root();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_block_generate_merkle_root_out(kth_block_const_t block, kth_hash_t* out_merkle) {
    auto hash_cpp = kth_chain_block_const_cpp(block).generate_merkle_root();
    kth::copy_c_hash(hash_cpp, out_merkle);
}

kth_bool_t kth_chain_block_is_extra_coinbases(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_extra_coinbases());
}

kth_bool_t kth_chain_block_is_final(kth_block_const_t block, kth_size_t height, uint32_t block_time) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_final(height, block_time));
}

kth_bool_t kth_chain_block_is_distinct_transaction_set(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_distinct_transaction_set());
}

kth_bool_t kth_chain_block_is_valid_coinbase_claim(kth_block_const_t block, kth_size_t height) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_valid_coinbase_claim(height));
}

kth_bool_t kth_chain_block_is_valid_coinbase_script(kth_block_const_t block, kth_size_t height) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_valid_coinbase_script(height));
}

kth_bool_t kth_chain_block_is_forward_reference(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_forward_reference());
}

kth_bool_t kth_chain_block_is_canonical_ordered(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_canonical_ordered());
}

kth_bool_t kth_chain_block_is_internal_double_spend(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_internal_double_spend());
}

kth_bool_t kth_chain_block_is_valid_merkle_root(kth_block_const_t block) {
    return kth::bool_to_int(kth_chain_block_const_cpp(block).is_valid_merkle_root());
}

kth_error_code_t kth_chain_block_check_transactions(kth_block_const_t block) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).check_transactions());
}

kth_error_code_t kth_chain_block_accept_transactions(kth_block_const_t block, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_sigops, kth_bool_t is_under_checkpoint) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).accept_transactions(flags, height, median_time_past, max_sigops, kth::int_to_bool(is_under_checkpoint)));
}

kth_error_code_t kth_chain_block_connect_transactions(kth_block_const_t block, kth_chain_state_const_t state) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).connect_transactions(kth_chain_chain_state_const_cpp(state)));
}

void kth_chain_block_reset(kth_block_mut_t block) {
    kth_chain_block_cpp(block).reset();
}

kth_size_t kth_chain_block_non_coinbase_input_count(kth_block_const_t block) {
    return kth_chain_block_const_cpp(block).non_coinbase_input_count();
}

kth_error_code_t kth_chain_block_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_block_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::block::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

uint8_t const* kth_chain_block_to_data(kth_block_const_t block, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_block_const_cpp(block).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_block_serialized_size(kth_block_const_t block, kth_bool_t wire) {
    return kth_chain_block_const_cpp(block).serialized_size(kth::int_to_bool(wire));
}

void kth_chain_block_set_transactions(kth_block_mut_t block, kth_transaction_list_const_t value) {
    kth_chain_block_cpp(block).set_transactions(kth_chain_transaction_list_const_cpp(value));
}

kth_block_mut_t kth_chain_block_genesis_mainnet() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_mainnet());
}

kth_block_mut_t kth_chain_block_genesis_testnet() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_testnet());
}

kth_block_mut_t kth_chain_block_genesis_regtest() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_regtest());
}

#if defined(KTH_CURRENCY_BCH)
kth_block_mut_t kth_chain_block_genesis_testnet4() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_testnet4());
}
#endif

#if defined(KTH_CURRENCY_BCH)
kth_block_mut_t kth_chain_block_genesis_scalenet() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_scalenet());
}
#endif

#if defined(KTH_CURRENCY_BCH)
kth_block_mut_t kth_chain_block_genesis_chipnet() {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::genesis_chipnet());
}
#endif

kth_size_t kth_chain_block_locator_size(kth_size_t top) {
    return kth::domain::chain::block::locator_size(top);
}

kth_block_indexes_mut_t kth_chain_block_locator_heights(kth_size_t top) {
    return kth::move_or_copy_and_leak(kth::domain::chain::block::locator_heights(top));
}

kth_size_t kth_chain_block_signature_operations(kth_block_const_t block) {
    return kth_chain_block_const_cpp(block).signature_operations();
}

kth_size_t kth_chain_block_total_inputs(kth_block_const_t block, kth_bool_t with_coinbase) {
    return kth_chain_block_const_cpp(block).total_inputs(kth::int_to_bool(with_coinbase));
}

kth_error_code_t kth_chain_block_check(kth_block_const_t block) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).check());
}

kth_error_code_t kth_chain_block_accept(kth_block_const_t block, kth_script_flags_t flags, kth_size_t height, uint32_t median_time_past, kth_size_t max_block_size_dynamic, kth_size_t max_sigops, kth_bool_t is_under_checkpoint, kth_bool_t transactions) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).accept(flags, height, median_time_past, max_block_size_dynamic, max_sigops, kth::int_to_bool(is_under_checkpoint), kth::int_to_bool(transactions)));
}

kth_error_code_t kth_chain_block_connect(kth_block_const_t block) {
    return kth::to_c_err(kth_chain_block_const_cpp(block).connect());
}

} // extern "C"
