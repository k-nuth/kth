// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CHAIN_COIN_SELECTION_H_
#define KTH_CAPI_CHAIN_COIN_SELECTION_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kth_coin_selection_algorithm_smallest_first,
    kth_coin_selection_algorithm_largest_first,
    kth_coin_selection_algorithm_manual,  // keeps the original UTXO order
    kth_coin_selection_algorithm_send_all,  // uses all available UTXOs
} kth_coin_selection_algorithm_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_COIN_SELECTION_H_ */
