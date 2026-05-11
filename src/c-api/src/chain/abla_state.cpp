// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/abla_state.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/abla.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::abla::state;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_abla_state_mut_t kth_chain_abla_state_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_abla_state_mut_t kth_chain_abla_state_construct(kth_abla_config_const_t cfg, uint64_t block_size) {
    KTH_PRECONDITION(cfg != nullptr);
    auto const& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    return kth::leak<cpp_t>(cfg_cpp, block_size);
}


// Destructor

void kth_chain_abla_state_destruct(kth_abla_state_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_abla_state_mut_t kth_chain_abla_state_copy(kth_abla_state_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

uint64_t kth_chain_abla_state_block_size(kth_abla_state_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).block_size;
}

uint64_t kth_chain_abla_state_control_block_size(kth_abla_state_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).control_block_size;
}

uint64_t kth_chain_abla_state_elastic_buffer_size(kth_abla_state_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).elastic_buffer_size;
}


// Setters

void kth_chain_abla_state_set_block_size(kth_abla_state_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).block_size = value;
}

void kth_chain_abla_state_set_control_block_size(kth_abla_state_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).control_block_size = value;
}

void kth_chain_abla_state_set_elastic_buffer_size(kth_abla_state_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).elastic_buffer_size = value;
}

} // extern "C"
