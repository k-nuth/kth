// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/abla_config.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/abla.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::abla::config;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_chain_abla_config_destruct(kth_abla_config_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_abla_config_mut_t kth_chain_abla_config_copy(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

uint64_t kth_chain_abla_config_epsilon0(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).epsilon0;
}

uint64_t kth_chain_abla_config_beta0(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).beta0;
}

uint64_t kth_chain_abla_config_n0(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).n0;
}

uint64_t kth_chain_abla_config_gamma_reciprocal(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).gamma_reciprocal;
}

uint64_t kth_chain_abla_config_zeta_xB7(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).zeta_xB7;
}

uint64_t kth_chain_abla_config_theta_reciprocal(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).theta_reciprocal;
}

uint64_t kth_chain_abla_config_delta(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).delta;
}

uint64_t kth_chain_abla_config_epsilon_max(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).epsilon_max;
}

uint64_t kth_chain_abla_config_beta_max(kth_abla_config_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).beta_max;
}


// Setters

void kth_chain_abla_config_set_epsilon0(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).epsilon0 = value;
}

void kth_chain_abla_config_set_beta0(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).beta0 = value;
}

void kth_chain_abla_config_set_n0(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).n0 = value;
}

void kth_chain_abla_config_set_gamma_reciprocal(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).gamma_reciprocal = value;
}

void kth_chain_abla_config_set_zeta_xB7(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).zeta_xB7 = value;
}

void kth_chain_abla_config_set_theta_reciprocal(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).theta_reciprocal = value;
}

void kth_chain_abla_config_set_delta(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).delta = value;
}

void kth_chain_abla_config_set_epsilon_max(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).epsilon_max = value;
}

void kth_chain_abla_config_set_beta_max(kth_abla_config_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).beta_max = value;
}

} // extern "C"
