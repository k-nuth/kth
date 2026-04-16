// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/output_list.h>

#include <kth/capi/chain/output.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_output_list_mut_t kth_chain_output_list_construct_default(void) {
    return new kth::domain::chain::output::list();
}

void kth_chain_output_list_push_back(kth_output_list_mut_t list, kth_output_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    kth::domain::chain::output tmp = kth::cpp_ref<kth::domain::chain::output>(elem);
    kth::cpp_ref<kth::domain::chain::output::list>(list).push_back(std::move(tmp));
}

void kth_chain_output_list_destruct(kth_output_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth::cpp_ref<kth::domain::chain::output::list>(list);
}

kth_size_t kth_chain_output_list_count(kth_output_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::cpp_ref<kth::domain::chain::output::list>(list).size();
}

kth_output_const_t kth_chain_output_list_nth(kth_output_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::cpp_ref<kth::domain::chain::output::list>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_chain_output_list_assign_at(kth_output_list_mut_t list, kth_size_t index, kth_output_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = kth::cpp_ref<kth::domain::chain::output::list>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::cpp_ref<kth::domain::chain::output>(elem);
}

void kth_chain_output_list_erase(kth_output_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::cpp_ref<kth::domain::chain::output::list>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
