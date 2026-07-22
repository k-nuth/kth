// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MINING_TEMPLATE_H_
#define KTH_CAPI_CHAIN_MINING_TEMPLATE_H_

#include <stdint.h>

#include <kth/capi/handles.h>

// The header fields of a mining template. The transaction selection is returned
// separately as an owned kth_transaction_list_mut_t, since it is variable-length
// (this struct stays a fixed POD).
typedef struct kth_mining_template {
    uint32_t version;
    kth_hash_t previous_block_hash;
    size_t height;
    uint32_t bits;
    uint32_t min_time;
    uint32_t current_time;
    uint64_t coinbase_value;
    uint64_t size_limit;
    uint64_t sigchecks_limit;
} kth_mining_template_t;

#endif // KTH_CAPI_CHAIN_MINING_TEMPLATE_H_
