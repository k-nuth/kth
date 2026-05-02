// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <expected>
#include <system_error>
#include <utility>

#include <kth/capi/wallet/coin_selection.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/coin_selection.hpp>

// Both `create_tx_template` overloads return the same
// `expected<tuple<tx, indices, addresses, amounts>, error_code>` shape
// and therefore unpack identically. Centralising the unpack here means
// a future tuple change (extra field, reorder) only edits one site.
namespace {

kth_error_code_t emit_tx_template_result(
    std::expected<kth::domain::wallet::tx_template_result, std::error_code>&& result,
    kth_transaction_mut_t* out_tx,
    kth_u32_list_mut_t* out_indices,
    kth_payment_address_list_mut_t* out_addresses,
    kth_u64_list_mut_t* out_amounts) {
    if ( ! result) return kth::to_c_err(result.error());
    *out_tx        = kth::leak(std::move(std::get<0>(*result)));
    *out_indices   = kth::leak(std::move(std::get<1>(*result)));
    *out_addresses = kth::leak(std::move(std::get<2>(*result)));
    *out_amounts   = kth::leak(std::move(std::get<3>(*result)));
    return kth_ec_success;
}

} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_error_code_t kth_wallet_coin_selection_select_utxos(kth_utxo_list_const_t available_utxos, uint64_t amount, kth_size_t outputs_size, kth_hash_t const* category, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const category_cpp = kth::hash_to_cpp(category->hash);
    auto const strategy_cpp = kth::coin_selection_strategy_to_cpp(strategy);
    auto result = kth::domain::wallet::select_utxos(available_utxos_cpp, amount, outputs_size_cpp, category_cpp, strategy_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_coin_selection_select_utxos_unsafe(kth_utxo_list_const_t available_utxos, uint64_t amount, kth_size_t outputs_size, uint8_t const* category, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const category_cpp = kth::hash_to_cpp(category);
    auto const strategy_cpp = kth::coin_selection_strategy_to_cpp(strategy);
    auto result = kth::domain::wallet::select_utxos(available_utxos_cpp, amount, outputs_size_cpp, category_cpp, strategy_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_coin_selection_select_utxos_send_all(kth_utxo_list_const_t available_utxos, kth_size_t outputs_size, kth_hash_t const* category, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const category_cpp = kth::hash_to_cpp(category->hash);
    auto result = kth::domain::wallet::select_utxos_send_all(available_utxos_cpp, outputs_size_cpp, category_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_coin_selection_select_utxos_send_all_unsafe(kth_utxo_list_const_t available_utxos, kth_size_t outputs_size, uint8_t const* category, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const category_cpp = kth::hash_to_cpp(category);
    auto result = kth::domain::wallet::select_utxos_send_all(available_utxos_cpp, outputs_size_cpp, category_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_coin_selection_select_utxos_both(kth_utxo_list_const_t available_utxos, uint64_t bch_amount, kth_hash_t const* token_category, uint64_t token_amount, kth_size_t outputs_size, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(token_category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const token_category_cpp = kth::hash_to_cpp(token_category->hash);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const strategy_cpp = kth::coin_selection_strategy_to_cpp(strategy);
    auto result = kth::domain::wallet::select_utxos_both(available_utxos_cpp, bch_amount, token_category_cpp, token_amount, outputs_size_cpp, strategy_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_coin_selection_select_utxos_both_unsafe(kth_utxo_list_const_t available_utxos, uint64_t bch_amount, uint8_t const* token_category, uint64_t token_amount, kth_size_t outputs_size, kth_coin_selection_strategy_t strategy, KTH_OUT_OWNED kth_coin_selection_result_mut_t* out) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(token_category != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const token_category_cpp = kth::hash_to_cpp(token_category);
    auto const outputs_size_cpp = kth::sz(outputs_size);
    auto const strategy_cpp = kth::coin_selection_strategy_to_cpp(strategy);
    auto result = kth::domain::wallet::select_utxos_both(available_utxos_cpp, bch_amount, token_category_cpp, token_amount, outputs_size_cpp, strategy_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_double_list_mut_t kth_wallet_coin_selection_make_change_ratios(kth_size_t change_count) {
    auto const change_count_cpp = kth::sz(change_count);
    return kth::leak_list<double>(kth::domain::wallet::make_change_ratios(change_count_cpp));
}

kth_error_code_t kth_wallet_coin_selection_create_tx_template(
    kth_utxo_list_const_t available_utxos,
    uint64_t amount_to_send,
    kth_payment_address_const_t destination_address,
    kth_payment_address_list_const_t change_addresses,
    kth_double_list_const_t change_ratios,
    kth_coin_selection_algorithm_t selection_algo,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_u32_list_mut_t* out_indices,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(destination_address != nullptr);
    KTH_PRECONDITION(change_addresses != nullptr);
    KTH_PRECONDITION(change_ratios != nullptr);
    KTH_PRECONDITION(out_tx != nullptr);
    KTH_PRECONDITION(*out_tx == nullptr);
    KTH_PRECONDITION(out_indices != nullptr);
    KTH_PRECONDITION(*out_indices == nullptr);
    KTH_PRECONDITION(out_addresses != nullptr);
    KTH_PRECONDITION(*out_addresses == nullptr);
    KTH_PRECONDITION(out_amounts != nullptr);
    KTH_PRECONDITION(*out_amounts == nullptr);
    auto available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const& destination_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination_address);
    auto const& change_addresses_cpp = kth::cpp_ref<std::vector<kth::domain::wallet::payment_address>>(change_addresses);
    auto const& change_ratios_cpp = kth::cpp_ref<std::vector<double>>(change_ratios);
    auto const algo_cpp = kth::coin_selection_algorithm_to_cpp(selection_algo);
    return emit_tx_template_result(
        kth::domain::wallet::create_tx_template(std::move(available_utxos_cpp), amount_to_send, destination_cpp, change_addresses_cpp, change_ratios_cpp, algo_cpp),
        out_tx, out_indices, out_addresses, out_amounts);
}

kth_error_code_t kth_wallet_coin_selection_create_tx_template_default_ratios(
    kth_utxo_list_const_t available_utxos,
    uint64_t amount_to_send,
    kth_payment_address_const_t destination_address,
    kth_payment_address_list_const_t change_addresses,
    kth_coin_selection_algorithm_t selection_algo,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_u32_list_mut_t* out_indices,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts) {
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(destination_address != nullptr);
    KTH_PRECONDITION(change_addresses != nullptr);
    KTH_PRECONDITION(out_tx != nullptr);
    KTH_PRECONDITION(*out_tx == nullptr);
    KTH_PRECONDITION(out_indices != nullptr);
    KTH_PRECONDITION(*out_indices == nullptr);
    KTH_PRECONDITION(out_addresses != nullptr);
    KTH_PRECONDITION(*out_addresses == nullptr);
    KTH_PRECONDITION(out_amounts != nullptr);
    KTH_PRECONDITION(*out_amounts == nullptr);
    auto available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const& destination_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination_address);
    auto const& change_addresses_cpp = kth::cpp_ref<std::vector<kth::domain::wallet::payment_address>>(change_addresses);
    auto const algo_cpp = kth::coin_selection_algorithm_to_cpp(selection_algo);
    return emit_tx_template_result(
        kth::domain::wallet::create_tx_template(std::move(available_utxos_cpp), amount_to_send, destination_cpp, change_addresses_cpp, algo_cpp),
        out_tx, out_indices, out_addresses, out_amounts);
}

kth_error_code_t kth_wallet_coin_selection_create_token_split_tx_template(
    kth_output_point_list_const_t outpoints_to_split,
    kth_utxo_list_const_t available_utxos,
    kth_payment_address_const_t destination_address,
    KTH_OUT_OWNED kth_transaction_mut_t* out_tx,
    KTH_OUT_OWNED kth_payment_address_list_mut_t* out_addresses,
    KTH_OUT_OWNED kth_u64_list_mut_t* out_amounts) {
    KTH_PRECONDITION(outpoints_to_split != nullptr);
    KTH_PRECONDITION(available_utxos != nullptr);
    KTH_PRECONDITION(destination_address != nullptr);
    KTH_PRECONDITION(out_tx != nullptr);
    KTH_PRECONDITION(*out_tx == nullptr);
    KTH_PRECONDITION(out_addresses != nullptr);
    KTH_PRECONDITION(*out_addresses == nullptr);
    KTH_PRECONDITION(out_amounts != nullptr);
    KTH_PRECONDITION(*out_amounts == nullptr);
    auto const& outpoints_cpp = kth::cpp_ref<std::vector<kth::domain::chain::output_point>>(outpoints_to_split);
    auto const& available_utxos_cpp = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(available_utxos);
    auto const& destination_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination_address);
    auto result = kth::domain::wallet::create_token_split_tx_template(outpoints_cpp, available_utxos_cpp, destination_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out_tx        = kth::leak(std::move(std::get<0>(*result)));
    *out_addresses = kth::leak(std::move(std::get<1>(*result)));
    *out_amounts   = kth::leak(std::move(std::get<2>(*result)));
    return kth_ec_success;
}

} // extern "C"
