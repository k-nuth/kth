// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/double_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// File-local alias so `kth::list_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = double;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

kth_double_list_mut_t kth_core_double_list_construct_default(void) {
    return kth::leak_list<cpp_t>();
}

void kth_core_double_list_push_back(kth_double_list_mut_t list, double elem) {
    KTH_PRECONDITION(list != nullptr);
    kth::list_ref<cpp_t>(list).push_back(elem);
}

void kth_core_double_list_destruct(kth_double_list_mut_t list) {
    kth::del_list<cpp_t>(list);
}

kth_size_t kth_core_double_list_count(kth_double_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::list_ref<cpp_t>(list).size();
}

double kth_core_double_list_nth(kth_double_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    return vec[index];
}

void kth_core_double_list_assign_at(kth_double_list_mut_t list, kth_size_t index, double elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = elem;
}

void kth_core_double_list_erase(kth_double_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
