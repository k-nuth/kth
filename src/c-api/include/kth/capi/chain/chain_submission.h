// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_CHAIN_SUBMISSION_H_
#define KTH_CAPI_CHAIN_CHAIN_SUBMISSION_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Operations

KTH_EXPORT
kth_error_code_t kth_chain_sync_submit_block(kth_chain_t self, kth_block_const_t block);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_CHAIN_SUBMISSION_H_
