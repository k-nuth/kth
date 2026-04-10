// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/u64_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_u64_list_mut_t kth_core_u64_list_construct_default(void) {
    return new std::vector<uint64_t>();
}

void kth_core_u64_list_push_back(kth_u64_list_mut_t list, uint64_t elem) {
    KTH_PRECONDITION(list != nullptr);
    static_cast<std::vector<uint64_t>*>(list)->push_back(elem);
}

void kth_core_u64_list_destruct(kth_u64_list_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<uint64_t>*>(list);
}

kth_size_t kth_core_u64_list_count(kth_u64_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<uint64_t> const*>(list)->size();
}

uint64_t kth_core_u64_list_nth(kth_u64_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<uint64_t> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return vec[index];
}

void kth_core_u64_list_assign_at(kth_u64_list_mut_t list, kth_size_t index, uint64_t elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<uint64_t>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = elem;
}

void kth_core_u64_list_erase(kth_u64_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<uint64_t>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
