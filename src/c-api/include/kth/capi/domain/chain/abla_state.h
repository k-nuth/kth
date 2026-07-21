// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DOMAIN_CHAIN_ABLA_STATE_H_
#define KTH_CAPI_DOMAIN_CHAIN_ABLA_STATE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_abla_state_mut_t`. Caller must release with `kth_chain_abla_state_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_state_mut_t kth_domain_chain_abla_state_construct_default(void);

/**
 * @return Owned `kth_abla_state_mut_t`. Caller must release with `kth_chain_abla_state_destruct`.
 * @param cfg Borrowed input. Copied by value into the resulting object; ownership of `cfg` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_abla_state_mut_t kth_domain_chain_abla_state_construct(kth_abla_config_const_t cfg, uint64_t block_size);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_domain_chain_abla_state_destruct(kth_abla_state_mut_t self);


// Copy

/** @return Owned `kth_abla_state_mut_t`. Caller must release with `kth_chain_abla_state_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_state_mut_t kth_domain_chain_abla_state_copy(kth_abla_state_const_t self);


// Getters

KTH_EXPORT
uint64_t kth_domain_chain_abla_state_block_size(kth_abla_state_const_t self);

KTH_EXPORT
uint64_t kth_domain_chain_abla_state_control_block_size(kth_abla_state_const_t self);

KTH_EXPORT
uint64_t kth_domain_chain_abla_state_elastic_buffer_size(kth_abla_state_const_t self);


// Setters

KTH_EXPORT
void kth_domain_chain_abla_state_set_block_size(kth_abla_state_mut_t self, uint64_t value);

KTH_EXPORT
void kth_domain_chain_abla_state_set_control_block_size(kth_abla_state_mut_t self, uint64_t value);

KTH_EXPORT
void kth_domain_chain_abla_state_set_elastic_buffer_size(kth_abla_state_mut_t self, uint64_t value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_DOMAIN_CHAIN_ABLA_STATE_H_
