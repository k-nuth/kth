// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MINING_INFO_H_
#define KTH_CAPI_CHAIN_MINING_INFO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Mirrors kth::blockchain::mining_info. The conversion names every field
// (mining_info_conversion.hpp) rather than copying the bytes. The C field order
// remains ABI; it is no longer coupled to the C++ declaration order.
//
// A composite diagnostic rather than a snapshot — blocks, difficulty and chain
// come from one published chain view, pooled_tx and the three flags are read
// live. Answerable while the node refuses to serve mining work, which is when
// it is worth reading.
typedef struct kth_mining_info {
    size_t blocks;
    double difficulty;
    size_t pooled_tx;
    uint32_t chain;
    bool transition_in_progress;   // a batch or reorganization is mutating
    bool caught_up;                // connected chain has reached its headers
    bool fresh;                    // that tip is within the configured age
} kth_mining_info_t;

#endif // KTH_CAPI_CHAIN_MINING_INFO_H_
