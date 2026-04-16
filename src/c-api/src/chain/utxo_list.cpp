// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/utxo_list.h>

#include <kth/capi/chain/utxo.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_utxo_list_mut_t kth_chain_utxo_list_construct_default(void) {
    return new std::vector<kth::domain::chain::utxo>();
}

void kth_chain_utxo_list_push_back(kth_utxo_list_mut_t list, kth_utxo_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    kth::domain::chain::utxo tmp = kth_chain_utxo_const_cpp(elem);
    kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list).push_back(std::move(tmp));
}

void kth_chain_utxo_list_destruct(kth_utxo_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
}

kth_size_t kth_chain_utxo_list_count(kth_utxo_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list).size();
}

kth_utxo_const_t kth_chain_utxo_list_nth(kth_utxo_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_chain_utxo_list_assign_at(kth_utxo_list_mut_t list, kth_size_t index, kth_utxo_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth_chain_utxo_const_cpp(elem);
}

void kth_chain_utxo_list_erase(kth_utxo_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
