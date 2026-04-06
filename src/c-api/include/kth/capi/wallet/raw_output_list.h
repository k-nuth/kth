// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_RAW_OUTPUT_LIST_H_
#define KTH_CAPI_WALLET_RAW_OUTPUT_LIST_H_

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_raw_output_list_t kth_wallet_raw_output_list_construct_default(void);

KTH_EXPORT
void kth_wallet_raw_output_list_push_back(kth_raw_output_list_t, kth_raw_output_t);

KTH_EXPORT
void kth_wallet_raw_output_list_destruct(kth_raw_output_list_t);

KTH_EXPORT
kth_size_t kth_wallet_raw_output_list_count(kth_raw_output_list_t);

KTH_EXPORT
kth_raw_output_t kth_wallet_raw_output_list_nth(kth_raw_output_list_t, kth_size_t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_WALLET_RAW_OUTPUT_LIST_H_ */
