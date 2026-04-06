// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/double_list.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Global converters (used by other generated code)
std::vector<double> const& kth_core_double_list_const_cpp(kth_double_list_const_t l) {
    return *static_cast<std::vector<double> const*>(l);
}

std::vector<double>& kth_core_double_list_cpp(kth_double_list_mut_t l) {
    return *static_cast<std::vector<double>*>(l);
}

// Construct from C++ (returns opaque pointer to existing vector)
kth_double_list_mut_t kth_core_double_list_construct_from_cpp(std::vector<double>& l) {
    return &l;
}

void const* kth_core_double_list_construct_from_cpp(std::vector<double> const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_double_list_mut_t kth_core_double_list_construct_default() {
    return new std::vector<double>();
}

void kth_core_double_list_push_back(kth_double_list_mut_t list, double elem) {
    kth_core_double_list_cpp(list).push_back(elem);
}

void kth_core_double_list_destruct(kth_double_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth_core_double_list_cpp(list);
}

kth_size_t kth_core_double_list_count(kth_double_list_const_t list) {
    return kth_core_double_list_const_cpp(list).size();
}

double kth_core_double_list_nth(kth_double_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_double_list_const_cpp(list).size());
    return kth_core_double_list_const_cpp(list)[index];
}

void kth_core_double_list_assign_at(kth_double_list_mut_t list, kth_size_t index, double elem) {
    KTH_PRECONDITION(index < kth_core_double_list_cpp(list).size());
    kth_core_double_list_cpp(list)[index] = elem;
}

void kth_core_double_list_erase(kth_double_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_double_list_cpp(list).size());
    auto& v = kth_core_double_list_cpp(list);
    v.erase(std::next(v.begin(), index));
}

} // extern "C"
