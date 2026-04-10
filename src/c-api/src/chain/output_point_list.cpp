// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/output_point_list.h>

#include <kth/capi/chain/output_point.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_output_point_list_mut_t kth_chain_output_point_list_construct_default(void) {
    return new std::vector<kth::domain::chain::output_point>();
}

void kth_chain_output_point_list_push_back(kth_output_point_list_mut_t list, kth_output_point_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    static_cast<std::vector<kth::domain::chain::output_point>*>(list)->push_back(kth_chain_output_point_const_cpp(elem));
}

void kth_chain_output_point_list_destruct(kth_output_point_list_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<kth::domain::chain::output_point>*>(list);
}

kth_size_t kth_chain_output_point_list_count(kth_output_point_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<kth::domain::chain::output_point> const*>(list)->size();
}

kth_output_point_const_t kth_chain_output_point_list_nth(kth_output_point_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<kth::domain::chain::output_point> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_chain_output_point_list_assign_at(kth_output_point_list_mut_t list, kth_size_t index, kth_output_point_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = *static_cast<std::vector<kth::domain::chain::output_point>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth_chain_output_point_const_cpp(elem);
}

void kth_chain_output_point_list_erase(kth_output_point_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<kth::domain::chain::output_point>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
