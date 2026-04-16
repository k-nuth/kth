// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MEMPOOL_TRANSACTION_LIST_H_
#define KTH_CAPI_CHAIN_MEMPOOL_TRANSACTION_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_mempool_transaction_list_t kth_chain_mempool_transaction_list_construct_default(void);

KTH_EXPORT
void kth_chain_mempool_transaction_list_push_back(kth_mempool_transaction_list_t l, kth_mempool_transaction_t e);

KTH_EXPORT
void kth_chain_mempool_transaction_list_destruct(kth_mempool_transaction_list_t l);

KTH_EXPORT
kth_size_t kth_chain_mempool_transaction_list_count(kth_mempool_transaction_list_t l);

KTH_EXPORT
kth_mempool_transaction_t kth_chain_mempool_transaction_list_nth(kth_mempool_transaction_list_t l, kth_size_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_MEMPOOL_TRANSACTION_LIST_H_ */
