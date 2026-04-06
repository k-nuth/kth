// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/payment_address_list.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

#include <kth/domain/wallet/payment_address.hpp>

using payment_address_vec = std::vector<kth::domain::wallet::payment_address>;

// Global converters
payment_address_vec const& kth_wallet_payment_address_list_const_cpp(kth_payment_address_list_t l) {
    return *static_cast<payment_address_vec const*>(l);
}

payment_address_vec& kth_wallet_payment_address_list_cpp(kth_payment_address_list_t l) {
    return *static_cast<payment_address_vec*>(l);
}

kth_payment_address_list_t kth_wallet_payment_address_list_construct_from_cpp(payment_address_vec& l) {
    return &l;
}

void const* kth_wallet_payment_address_list_construct_from_cpp(payment_address_vec const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_payment_address_list_t kth_wallet_payment_address_list_construct_default() {
    return new payment_address_vec();
}

void kth_wallet_payment_address_list_push_back(kth_payment_address_list_t list, kth_payment_address_t elem) {
    kth_wallet_payment_address_list_cpp(list).push_back(kth_wallet_payment_address_const_cpp(elem));
}

void kth_wallet_payment_address_list_destruct(kth_payment_address_list_t list) {
    delete &kth_wallet_payment_address_list_cpp(list);
}

kth_size_t kth_wallet_payment_address_list_count(kth_payment_address_list_t list) {
    return kth_wallet_payment_address_list_const_cpp(list).size();
}

kth_payment_address_t kth_wallet_payment_address_list_nth(kth_payment_address_list_t list, kth_size_t index) {
    auto& x = kth_wallet_payment_address_list_cpp(list)[index];
    return &x;
}

} // extern "C"
