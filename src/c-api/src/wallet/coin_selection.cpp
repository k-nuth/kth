// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/wallet/coin_selection.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/coin_selection.hpp>

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

} // extern "C"
