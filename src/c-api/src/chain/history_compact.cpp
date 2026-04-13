// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/history_compact.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/history.hpp>

// Conversion functions
kth::domain::chain::history_compact& kth_chain_history_compact_mut_cpp(kth_history_compact_mut_t o) {
    return *static_cast<kth::domain::chain::history_compact*>(o);
}
kth::domain::chain::history_compact const& kth_chain_history_compact_const_cpp(kth_history_compact_const_t o) {
    return *static_cast<kth::domain::chain::history_compact const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_chain_history_compact_destruct(kth_history_compact_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_history_compact_mut_cpp(self);
}


// Copy

kth_history_compact_mut_t kth_chain_history_compact_copy(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::history_compact(kth_chain_history_compact_const_cpp(self));
}


// Getters

kth_point_kind_t kth_chain_history_compact_kind(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_point_kind_t>(kth_chain_history_compact_const_cpp(self).kind);
}

kth_point_const_t kth_chain_history_compact_point(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_history_compact_const_cpp(self).point);
}

uint32_t kth_chain_history_compact_height(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_history_compact_const_cpp(self).height;
}

uint64_t kth_chain_history_compact_value_or_previous_checksum(kth_history_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_history_compact_const_cpp(self).value;
}


// Setters

void kth_chain_history_compact_set_kind(kth_history_compact_mut_t self, kth_point_kind_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = static_cast<kth::domain::chain::point_kind>(value);
    kth_chain_history_compact_mut_cpp(self).kind = value_cpp;
}

void kth_chain_history_compact_set_point(kth_history_compact_mut_t self, kth_point_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_chain_point_const_cpp(value);
    kth_chain_history_compact_mut_cpp(self).point = value_cpp;
}

void kth_chain_history_compact_set_height(kth_history_compact_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_history_compact_mut_cpp(self).height = value;
}

void kth_chain_history_compact_set_value_or_previous_checksum(kth_history_compact_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_history_compact_mut_cpp(self).value = value;
}

} // extern "C"
