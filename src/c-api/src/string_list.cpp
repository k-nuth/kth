// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/string_list.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Global converters (used by other generated code)
std::vector<std::string> const& kth_core_string_list_const_cpp(kth_string_list_const_t l) {
    return *static_cast<std::vector<std::string> const*>(l);
}

std::vector<std::string>& kth_core_string_list_cpp(kth_string_list_mut_t l) {
    return *static_cast<std::vector<std::string>*>(l);
}

// Construct from C++ (returns opaque pointer to existing vector)
kth_string_list_mut_t kth_core_string_list_construct_from_cpp(std::vector<std::string>& l) {
    return &l;
}

void const* kth_core_string_list_construct_from_cpp(std::vector<std::string> const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_string_list_mut_t kth_core_string_list_construct_default() {
    return new std::vector<std::string>();
}

void kth_core_string_list_push_back(kth_string_list_mut_t list, char const* elem) {
    KTH_PRECONDITION(elem != nullptr);
    kth_core_string_list_cpp(list).push_back(std::string(elem));
}

void kth_core_string_list_destruct(kth_string_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth_core_string_list_cpp(list);
}

kth_size_t kth_core_string_list_count(kth_string_list_const_t list) {
    return kth_core_string_list_const_cpp(list).size();
}

char const* kth_core_string_list_nth(kth_string_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_string_list_const_cpp(list).size());
    return kth_core_string_list_const_cpp(list)[index].c_str();
}

void kth_core_string_list_assign_at(kth_string_list_mut_t list, kth_size_t index, char const* elem) {
    KTH_PRECONDITION(elem != nullptr);
    KTH_PRECONDITION(index < kth_core_string_list_cpp(list).size());
    kth_core_string_list_cpp(list)[index] = std::string(elem);
}

void kth_core_string_list_erase(kth_string_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_string_list_cpp(list).size());
    auto& v = kth_core_string_list_cpp(list);
    v.erase(std::next(v.begin(), index));
}

} // extern "C"
