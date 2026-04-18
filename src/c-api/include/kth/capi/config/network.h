// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CONFIG_NETWORK_H_
#define KTH_CAPI_CONFIG_NETWORK_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kth_network_mainnet = 0,
    kth_network_testnet = 1,
    kth_network_regtest = 2,
#if defined(KTH_CURRENCY_BCH)
    kth_network_testnet4 = 3,
    kth_network_scalenet = 4,
    kth_network_chipnet = 5,
#endif
} kth_network_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CONFIG_NETWORK_H_ */
