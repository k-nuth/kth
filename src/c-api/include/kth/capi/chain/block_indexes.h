// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_BLOCK_INDEXES_H_
#define KTH_CAPI_CHAIN_BLOCK_INDEXES_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Owned `kth_block_indexes_mut_t`. Caller must release with `kth_chain_block_indexes_destruct`. */
KTH_EXPORT KTH_OWNED
kth_block_indexes_mut_t kth_chain_block_indexes_construct_default(void);

KTH_EXPORT
void kth_chain_block_indexes_push_back(kth_block_indexes_mut_t list, kth_size_t elem);

KTH_EXPORT
void kth_chain_block_indexes_destruct(kth_block_indexes_mut_t list);

KTH_EXPORT
kth_size_t kth_chain_block_indexes_count(kth_block_indexes_const_t list);

KTH_EXPORT
kth_size_t kth_chain_block_indexes_nth(kth_block_indexes_const_t list, kth_size_t index);

KTH_EXPORT
void kth_chain_block_indexes_assign_at(kth_block_indexes_mut_t list, kth_size_t index, kth_size_t elem);

KTH_EXPORT
void kth_chain_block_indexes_erase(kth_block_indexes_mut_t list, kth_size_t index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_BLOCK_INDEXES_H_ */
