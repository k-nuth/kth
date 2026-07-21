// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MEMPOOL_ENTRY_INFO_H_
#define KTH_CAPI_CHAIN_MEMPOOL_ENTRY_INFO_H_

#include <stdint.h>

typedef struct kth_mempool_entry_info {
    uint64_t fee;
    uint32_t size;
    uint64_t time;
} kth_mempool_entry_info_t;

#endif // KTH_CAPI_CHAIN_MEMPOOL_ENTRY_INFO_H_
