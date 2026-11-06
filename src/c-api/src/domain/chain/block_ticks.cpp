// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/domain/chain/block_ticks.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/block_ticks.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::block_ticks;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_block_ticks_mut_t kth_domain_chain_block_ticks_construct_default(void) {
    return kth::make_owned<cpp_t>();
}

kth_block_ticks_mut_t kth_domain_chain_block_ticks_activated_at(uint64_t activation_height) {
    return kth::to_owned(cpp_t::activated_at(activation_height));
}


// Destructor

void kth_domain_chain_block_ticks_destruct(kth_block_ticks_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_block_ticks_mut_t kth_domain_chain_block_ticks_copy(kth_block_ticks_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_domain_chain_block_ticks_equals(kth_block_ticks_const_t self, kth_block_ticks_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_domain_chain_block_ticks_not_equal(kth_block_ticks_const_t self, kth_block_ticks_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Operations

uint64_t kth_domain_chain_block_ticks_height_to_tick(kth_block_ticks_const_t self, uint64_t height) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).height_to_tick(height);
}

uint64_t kth_domain_chain_block_ticks_tick_to_height(kth_block_ticks_const_t self, uint64_t tick) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).tick_to_height(tick);
}

uint64_t kth_domain_chain_block_ticks_block_interval(kth_block_ticks_const_t self, uint64_t height) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).block_interval(height);
}

uint64_t kth_domain_chain_block_ticks_next_block_interval(kth_block_ticks_const_t self, uint64_t height) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).next_block_interval(height);
}

uint64_t kth_domain_chain_block_ticks_next_block_tick(kth_block_ticks_const_t self, uint64_t tip) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).next_block_tick(tip);
}

} // extern "C"
