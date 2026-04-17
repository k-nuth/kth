// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/hash_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// File-local alias so `kth::list_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::hash_digest;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

kth_hash_list_mut_t kth_core_hash_list_construct_default(void) {
    return kth::leak_list<cpp_t>();
}

void kth_core_hash_list_push_back(kth_hash_list_mut_t list, kth_hash_t elem) {
    KTH_PRECONDITION(list != nullptr);
    kth::list_ref<cpp_t>(list).push_back(kth::to_array(elem.hash));
}

void kth_core_hash_list_destruct(kth_hash_list_mut_t list) {
    kth::del_list<cpp_t>(list);
}

kth_size_t kth_core_hash_list_count(kth_hash_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::list_ref<cpp_t>(list).size();
}

kth_hash_t kth_core_hash_list_nth(kth_hash_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    return kth::to_hash_t(vec[index]);
}

void kth_core_hash_list_assign_at(kth_hash_list_mut_t list, kth_size_t index, kth_hash_t elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::to_array(elem.hash);
}

void kth_core_hash_list_erase(kth_hash_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

void kth_core_hash_list_nth_out(kth_hash_list_const_t list, kth_size_t n, kth_hash_t* out_hash) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    auto const& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(n < vec.size());
    kth::copy_c_hash(vec[n], out_hash);
}

} // extern "C"
