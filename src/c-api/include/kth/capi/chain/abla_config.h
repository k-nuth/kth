// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_ABLA_CONFIG_H_
#define KTH_CAPI_CHAIN_ABLA_CONFIG_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_abla_config_destruct(kth_abla_config_mut_t self);


// Copy

/** @return Owned `kth_abla_config_mut_t`. Caller must release with `kth_chain_abla_config_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_config_mut_t kth_chain_abla_config_copy(kth_abla_config_const_t self);


// Getters

KTH_EXPORT
uint64_t kth_chain_abla_config_epsilon0(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_beta0(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_n0(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_gamma_reciprocal(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_zeta_xB7(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_theta_reciprocal(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_delta(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_epsilon_max(kth_abla_config_const_t self);

KTH_EXPORT
uint64_t kth_chain_abla_config_beta_max(kth_abla_config_const_t self);


// Setters

KTH_EXPORT
void kth_chain_abla_config_set_epsilon0(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_beta0(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_n0(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_gamma_reciprocal(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_zeta_xB7(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_theta_reciprocal(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_delta(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_epsilon_max(kth_abla_config_mut_t self, uint64_t value);

KTH_EXPORT
void kth_chain_abla_config_set_beta_max(kth_abla_config_mut_t self, uint64_t value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_ABLA_CONFIG_H_
