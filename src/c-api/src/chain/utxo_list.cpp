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
    return new kth::domain::chain::utxo::list();
}

void kth_chain_utxo_list_push_back(kth_utxo_list_mut_t list, kth_utxo_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    kth::domain::chain::utxo tmp = kth::cpp_ref<kth::domain::chain::utxo>(elem);
    kth::cpp_ref<kth::domain::chain::utxo::list>(list).push_back(std::move(tmp));
}

void kth_chain_utxo_list_destruct(kth_utxo_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth::cpp_ref<kth::domain::chain::utxo::list>(list);
}

kth_size_t kth_chain_utxo_list_count(kth_utxo_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::cpp_ref<kth::domain::chain::utxo::list>(list).size();
}

kth_utxo_const_t kth_chain_utxo_list_nth(kth_utxo_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::cpp_ref<kth::domain::chain::utxo::list>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_chain_utxo_list_assign_at(kth_utxo_list_mut_t list, kth_size_t index, kth_utxo_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = kth::cpp_ref<kth::domain::chain::utxo::list>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::cpp_ref<kth::domain::chain::utxo>(elem);
}

void kth_chain_utxo_list_erase(kth_utxo_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::cpp_ref<kth::domain::chain::utxo::list>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
