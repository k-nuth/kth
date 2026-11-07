// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DOMAIN_CHAIN_BLOCK_TICKS_H_
#define KTH_CAPI_DOMAIN_CHAIN_BLOCK_TICKS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

typedef void* kth_block_ticks_mut_t;
typedef void const* kth_block_ticks_const_t;

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_block_ticks_mut_t`. Caller must release with `kth_domain_chain_block_ticks_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_ticks_mut_t kth_domain_chain_block_ticks_construct_default(void);

/** @return Owned `kth_block_ticks_mut_t`. Caller must release with `kth_domain_chain_block_ticks_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_ticks_mut_t kth_domain_chain_block_ticks_activated_at(uint64_t activation_height);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_domain_chain_block_ticks_destruct(kth_block_ticks_mut_t self);


// Copy

/** @return Owned `kth_block_ticks_mut_t`. Caller must release with `kth_domain_chain_block_ticks_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_ticks_mut_t kth_domain_chain_block_ticks_copy(kth_block_ticks_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_domain_chain_block_ticks_equals(kth_block_ticks_const_t self, kth_block_ticks_const_t other);

KTH_EXPORT
kth_bool_t kth_domain_chain_block_ticks_not_equal(kth_block_ticks_const_t self, kth_block_ticks_const_t other);


// Operations

KTH_EXPORT
uint64_t kth_domain_chain_block_ticks_height_to_tick(kth_block_ticks_const_t self, uint64_t height);

KTH_EXPORT
uint64_t kth_domain_chain_block_ticks_tick_to_height(kth_block_ticks_const_t self, uint64_t tick);

KTH_EXPORT
uint64_t kth_domain_chain_block_ticks_block_interval(kth_block_ticks_const_t self, uint64_t height);

KTH_EXPORT
uint64_t kth_domain_chain_block_ticks_next_block_interval(kth_block_ticks_const_t self, uint64_t height);

KTH_EXPORT
uint64_t kth_domain_chain_block_ticks_next_block_tick(kth_block_ticks_const_t self, uint64_t tip);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_DOMAIN_CHAIN_BLOCK_TICKS_H_
