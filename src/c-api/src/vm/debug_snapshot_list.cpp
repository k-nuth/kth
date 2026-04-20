// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/debug_snapshot_list.h>

#include <kth/capi/vm/debug_snapshot.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// File-local alias so `kth::list_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::debug_snapshot;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

kth_debug_snapshot_list_mut_t kth_vm_debug_snapshot_list_construct_default(void) {
    return kth::leak_list<cpp_t>();
}

void kth_vm_debug_snapshot_list_push_back(kth_debug_snapshot_list_mut_t list, kth_debug_snapshot_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto tmp = kth::cpp_ref<cpp_t>(elem);
    kth::list_ref<cpp_t>(list).push_back(std::move(tmp));
}

void kth_vm_debug_snapshot_list_destruct(kth_debug_snapshot_list_mut_t list) {
    kth::del_list<cpp_t>(list);
}

kth_size_t kth_vm_debug_snapshot_list_count(kth_debug_snapshot_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::list_ref<cpp_t>(list).size();
}

kth_debug_snapshot_const_t kth_vm_debug_snapshot_list_nth(kth_debug_snapshot_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    return &vec[index];
}

void kth_vm_debug_snapshot_list_assign_at(kth_debug_snapshot_list_mut_t list, kth_size_t index, kth_debug_snapshot_const_t elem) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::cpp_ref<cpp_t>(elem);
}

void kth_vm_debug_snapshot_list_erase(kth_debug_snapshot_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
