// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/hash_list.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Global converters (used by other generated code)
std::vector<kth::hash_digest> const& kth_core_hash_list_const_cpp(kth_hash_list_const_t l) {
    return *static_cast<std::vector<kth::hash_digest> const*>(l);
}

std::vector<kth::hash_digest>& kth_core_hash_list_cpp(kth_hash_list_mut_t l) {
    return *static_cast<std::vector<kth::hash_digest>*>(l);
}

// Construct from C++ (returns opaque pointer to existing vector)
kth_hash_list_mut_t kth_core_hash_list_construct_from_cpp(std::vector<kth::hash_digest>& l) {
    return &l;
}

void const* kth_core_hash_list_construct_from_cpp(std::vector<kth::hash_digest> const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_hash_list_mut_t kth_core_hash_list_construct_default() {
    return new std::vector<kth::hash_digest>();
}

void kth_core_hash_list_push_back(kth_hash_list_mut_t list, kth_hash_t elem) {
    kth_core_hash_list_cpp(list).push_back(kth::to_array(elem.hash));
}

void kth_core_hash_list_destruct(kth_hash_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth_core_hash_list_cpp(list);
}

kth_size_t kth_core_hash_list_count(kth_hash_list_const_t list) {
    return kth_core_hash_list_const_cpp(list).size();
}

kth_hash_t kth_core_hash_list_nth(kth_hash_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_hash_list_const_cpp(list).size());
    return kth::to_hash_t(kth_core_hash_list_const_cpp(list)[index]);
}

void kth_core_hash_list_assign_at(kth_hash_list_mut_t list, kth_size_t index, kth_hash_t elem) {
    KTH_PRECONDITION(index < kth_core_hash_list_cpp(list).size());
    kth_core_hash_list_cpp(list)[index] = kth::to_array(elem.hash);
}

void kth_core_hash_list_erase(kth_hash_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_core_hash_list_cpp(list).size());
    auto& v = kth_core_hash_list_cpp(list);
    v.erase(std::next(v.begin(), index));
}

} // extern "C"
