// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/hash_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_hash_list_mut_t kth_core_hash_list_construct_default(void) {
    return new std::vector<kth::hash_digest>();
}

void kth_core_hash_list_push_back(kth_hash_list_mut_t list, kth_hash_t elem) {
    KTH_PRECONDITION(list != nullptr);
    static_cast<std::vector<kth::hash_digest>*>(list)->push_back(kth::to_array(elem.hash));
}

void kth_core_hash_list_destruct(kth_hash_list_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<kth::hash_digest>*>(list);
}

kth_size_t kth_core_hash_list_count(kth_hash_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<kth::hash_digest> const*>(list)->size();
}

kth_hash_t kth_core_hash_list_nth(kth_hash_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<kth::hash_digest> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return kth::to_hash_t(vec[index]);
}

void kth_core_hash_list_assign_at(kth_hash_list_mut_t list, kth_size_t index, kth_hash_t elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<kth::hash_digest>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::to_array(elem.hash);
}

void kth_core_hash_list_erase(kth_hash_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<kth::hash_digest>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

void kth_core_hash_list_nth_out(kth_hash_list_const_t list, kth_size_t n, kth_hash_t* out_hash) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    auto const& vec = *static_cast<std::vector<kth::hash_digest> const*>(list);
    KTH_PRECONDITION(n < vec.size());
    kth::copy_c_hash(vec[n], out_hash);
}

} // extern "C"
