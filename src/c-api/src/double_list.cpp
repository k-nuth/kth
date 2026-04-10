// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/double_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_double_list_mut_t kth_core_double_list_construct_default(void) {
    return new std::vector<double>();
}

void kth_core_double_list_push_back(kth_double_list_mut_t list, double elem) {
    KTH_PRECONDITION(list != nullptr);
    static_cast<std::vector<double>*>(list)->push_back(elem);
}

void kth_core_double_list_destruct(kth_double_list_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<double>*>(list);
}

kth_size_t kth_core_double_list_count(kth_double_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<double> const*>(list)->size();
}

double kth_core_double_list_nth(kth_double_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<double> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return vec[index];
}

void kth_core_double_list_assign_at(kth_double_list_mut_t list, kth_size_t index, double elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<double>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = elem;
}

void kth_core_double_list_erase(kth_double_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<double>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
