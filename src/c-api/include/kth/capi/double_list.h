// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_DOUBLE_LIST_H_
#define KTH_CAPI_DOUBLE_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_double_list_mut_t kth_core_double_list_construct_default(void);

KTH_EXPORT
void kth_core_double_list_push_back(kth_double_list_mut_t list, double elem);

KTH_EXPORT
void kth_core_double_list_destruct(kth_double_list_mut_t list);

KTH_EXPORT
kth_size_t kth_core_double_list_count(kth_double_list_const_t list);

KTH_EXPORT
double kth_core_double_list_nth(kth_double_list_const_t list, kth_size_t index);

KTH_EXPORT
void kth_core_double_list_assign_at(kth_double_list_mut_t list, kth_size_t index, double elem);

KTH_EXPORT
void kth_core_double_list_erase(kth_double_list_mut_t list, kth_size_t index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_DOUBLE_LIST_H_ */
