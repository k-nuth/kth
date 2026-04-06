// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/raw_output_list.h>

#include <vector>

#include <kth/domain/wallet/transaction_functions.hpp>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

using raw_output_vec = std::vector<kth::domain::wallet::raw_output>;

// Global converters
raw_output_vec const& kth_wallet_raw_output_list_const_cpp(kth_raw_output_list_t l) {
    return *static_cast<raw_output_vec const*>(l);
}

raw_output_vec& kth_wallet_raw_output_list_cpp(kth_raw_output_list_t l) {
    return *static_cast<raw_output_vec*>(l);
}

kth_raw_output_list_t kth_wallet_raw_output_list_construct_from_cpp(raw_output_vec& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_raw_output_list_t kth_wallet_raw_output_list_construct_default() {
    return new raw_output_vec();
}

void kth_wallet_raw_output_list_push_back(kth_raw_output_list_t list, kth_raw_output_t elem) {
    kth_wallet_raw_output_list_cpp(list).push_back(kth_wallet_raw_output_const_cpp(elem));
}

void kth_wallet_raw_output_list_destruct(kth_raw_output_list_t list) {
    delete &kth_wallet_raw_output_list_cpp(list);
}

kth_size_t kth_wallet_raw_output_list_count(kth_raw_output_list_t list) {
    return kth_wallet_raw_output_list_const_cpp(list).size();
}

kth_raw_output_t kth_wallet_raw_output_list_nth(kth_raw_output_list_t list, kth_size_t index) {
    auto& x = kth_wallet_raw_output_list_cpp(list)[index];
    return &x;
}

} // extern "C"
