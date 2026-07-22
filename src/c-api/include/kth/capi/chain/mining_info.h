// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MINING_INFO_H_
#define KTH_CAPI_CHAIN_MINING_INFO_H_

#include <stdint.h>

typedef struct kth_mining_info {
    size_t blocks;
    double difficulty;
    size_t pooled_tx;
    uint32_t chain;
} kth_mining_info_t;

#endif // KTH_CAPI_CHAIN_MINING_INFO_H_
