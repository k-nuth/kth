// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_CHAIN_OPERATION_LIST_H_
#define KTH_CAPI_CHAIN_OPERATION_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_operation_list_mut_t kth_chain_operation_list_construct_default(void);

KTH_EXPORT
void kth_chain_operation_list_push_back(kth_operation_list_mut_t list, kth_operation_const_t elem);

KTH_EXPORT
void kth_chain_operation_list_destruct(kth_operation_list_mut_t list);

KTH_EXPORT
kth_size_t kth_chain_operation_list_count(kth_operation_list_const_t list);

KTH_EXPORT
kth_operation_const_t kth_chain_operation_list_nth(kth_operation_list_const_t list, kth_size_t index);

KTH_EXPORT
void kth_chain_operation_list_assign_at(kth_operation_list_mut_t list, kth_size_t index, kth_operation_const_t elem);

KTH_EXPORT
void kth_chain_operation_list_erase(kth_operation_list_mut_t list, kth_size_t index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_OPERATION_LIST_H_ */
