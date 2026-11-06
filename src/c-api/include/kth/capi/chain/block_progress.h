// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_BLOCK_PROGRESS_H_
#define KTH_CAPI_CHAIN_BLOCK_PROGRESS_H_

#include <stddef.h>
#include <stdint.h>
#include <kth/capi/handles.h>

// Branch-local block tick metadata and depth against one connected chain view.
typedef struct kth_block_progress {
    size_t height;
    uint64_t ticks;
    uint64_t target_time;
    uint64_t next_target_time;
    int64_t confirmations;
    int64_t confirmations_ticks;
    kth_hash_t block_hash;
} kth_block_progress_t;

#endif // KTH_CAPI_CHAIN_BLOCK_PROGRESS_H_
