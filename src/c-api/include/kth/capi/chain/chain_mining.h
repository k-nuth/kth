// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_CHAIN_MINING_H_
#define KTH_CAPI_CHAIN_CHAIN_MINING_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/mining_info.h>

#ifdef __cplusplus
extern "C" {
#endif

// Getters

KTH_EXPORT
kth_error_code_t kth_chain_sync_mining_info(kth_chain_t self, kth_mining_info_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_CHAIN_MINING_H_
