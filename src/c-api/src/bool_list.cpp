// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/bool_list.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// File-local alias so `kth::list_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
//
// NOTE: `cpp_t = bool` instantiates the standard-library
// `std::vector<bool>` specialization, which uses a proxy
// reference type. Every access below (`push_back(bool)`,
// `vec[i] = <bool>`, `bool_to_int(vec[i])`, `vec.erase(...)`)
// goes through the proxy correctly. Future accessors that
// return `bool&` / take the address of an element would
// silently break — that's the trade-off of staying on the
// specialisation instead of backing the container with
// `std::vector<uint8_t>`.
namespace {
using cpp_t = bool;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

kth_bool_list_mut_t kth_core_bool_list_construct_default(void) {
    return kth::leak_list<cpp_t>();
}

void kth_core_bool_list_push_back(kth_bool_list_mut_t list, kth_bool_t elem) {
    KTH_PRECONDITION(list != nullptr);
    kth::list_ref<cpp_t>(list).push_back(kth::int_to_bool(elem));
}

void kth_core_bool_list_destruct(kth_bool_list_mut_t list) {
    kth::del_list<cpp_t>(list);
}

kth_size_t kth_core_bool_list_count(kth_bool_list_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::list_ref<cpp_t>(list).size();
}

kth_bool_t kth_core_bool_list_nth(kth_bool_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    return kth::bool_to_int(vec[index]);
}

void kth_core_bool_list_assign_at(kth_bool_list_mut_t list, kth_size_t index, kth_bool_t elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = kth::int_to_bool(elem);
}

void kth_core_bool_list_erase(kth_bool_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::list_ref<cpp_t>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
