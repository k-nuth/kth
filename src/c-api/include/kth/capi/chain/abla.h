// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_ABLA_H_
#define KTH_CAPI_CHAIN_ABLA_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/abla_config_validity.h>
#include <kth/capi/chain/abla_state_validity.h>

#ifdef __cplusplus
extern "C" {
#endif

// Setters

KTH_EXPORT
void kth_chain_abla_set_max(kth_abla_config_mut_t cfg);


// Static utilities

/** @return Owned `kth_abla_config_mut_t`. Caller must release with `kth_chain_abla_config_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_config_mut_t kth_chain_abla_default_config(uint64_t default_block_size, kth_bool_t fixed_size);

KTH_EXPORT
kth_abla_config_validity_t kth_chain_abla_validate_config(kth_abla_config_const_t cfg);

KTH_EXPORT
uint64_t kth_chain_abla_block_size_limit(kth_abla_state_const_t st);

/** @return Owned `kth_abla_state_mut_t`. Caller must release with `kth_chain_abla_state_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_state_mut_t kth_chain_abla_next(kth_abla_state_const_t st, kth_abla_config_const_t cfg, uint64_t next_block_size);

/** @return Owned `kth_abla_state_mut_t`. Caller must release with `kth_chain_abla_state_destruct`. */
KTH_EXPORT KTH_OWNED
kth_abla_state_mut_t kth_chain_abla_lookahead(kth_abla_state_const_t st, kth_abla_config_const_t cfg, kth_size_t count);

KTH_EXPORT
kth_abla_state_validity_t kth_chain_abla_validate_state(kth_abla_state_const_t st, kth_abla_config_const_t cfg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_ABLA_H_
