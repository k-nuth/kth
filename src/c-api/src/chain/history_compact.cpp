// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/history_compact.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/history.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::history_compact;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_chain_history_compact_destruct(kth_history_compact_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_history_compact_mut_t kth_chain_history_compact_copy(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_point_kind_t kth_chain_history_compact_kind(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::point_kind_to_c(kth::cpp_ref<cpp_t>(self).kind);
}

kth_point_const_t kth_chain_history_compact_point(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).point);
}

uint32_t kth_chain_history_compact_height(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).height;
}

uint64_t kth_chain_history_compact_value_or_previous_checksum(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).value;
}


// Setters

void kth_chain_history_compact_set_kind(kth_history_compact_mut_t self, kth_point_kind_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::point_kind_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).kind = value_cpp;
}

void kth_chain_history_compact_set_point(kth_history_compact_mut_t self, kth_point_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::point>(value);
    kth::cpp_ref<cpp_t>(self).point = value_cpp;
}

void kth_chain_history_compact_set_height(kth_history_compact_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).height = value;
}

void kth_chain_history_compact_set_value_or_previous_checksum(kth_history_compact_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).value = value;
}

} // extern "C"
