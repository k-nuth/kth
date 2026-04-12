// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/payment_address_list.h>

#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_payment_address_list_mut_t kth_wallet_payment_address_list_construct_default(void) {
    return new std::vector<kth::domain::wallet::payment_address>();
}

void kth_wallet_payment_address_list_push_back(kth_payment_address_list_mut_t list, kth_payment_address_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    static_cast<std::vector<kth::domain::wallet::payment_address>*>(list)->push_back(kth_wallet_payment_address_const_cpp(elem));
}

void kth_wallet_payment_address_list_destruct(kth_payment_address_list_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<kth::domain::wallet::payment_address>*>(list);
}

kth_size_t kth_wallet_payment_address_list_count(kth_payment_address_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<kth::domain::wallet::payment_address> const*>(list)->size();
}

kth_payment_address_const_t kth_wallet_payment_address_list_nth(kth_payment_address_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<kth::domain::wallet::payment_address> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_wallet_payment_address_list_assign_at(kth_payment_address_list_mut_t list, kth_size_t index, kth_payment_address_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = *static_cast<std::vector<kth::domain::wallet::payment_address>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth_wallet_payment_address_const_cpp(elem);
}

void kth_wallet_payment_address_list_erase(kth_payment_address_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<kth::domain::wallet::payment_address>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
