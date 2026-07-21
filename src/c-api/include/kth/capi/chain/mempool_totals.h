// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MEMPOOL_TOTALS_H_
#define KTH_CAPI_CHAIN_MEMPOOL_TOTALS_H_

#include <stdint.h>

typedef struct kth_mempool_totals {
    size_t size;
    uint64_t bytes;
    uint64_t total_fee;
} kth_mempool_totals_t;

#endif // KTH_CAPI_CHAIN_MEMPOOL_TOTALS_H_
