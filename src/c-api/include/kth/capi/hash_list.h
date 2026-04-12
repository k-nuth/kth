// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_HASH_LIST_H_
#define KTH_CAPI_HASH_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_core_hash_list_construct_default(void);

KTH_EXPORT
void kth_core_hash_list_push_back(kth_hash_list_mut_t list, kth_hash_t elem);

/** No-op if `list` is null. */
KTH_EXPORT
void kth_core_hash_list_destruct(kth_hash_list_mut_t list);

KTH_EXPORT
kth_size_t kth_core_hash_list_count(kth_hash_list_const_t list);

KTH_EXPORT
kth_hash_t kth_core_hash_list_nth(kth_hash_list_const_t list, kth_size_t index);

KTH_EXPORT
void kth_core_hash_list_assign_at(kth_hash_list_mut_t list, kth_size_t index, kth_hash_t elem);

KTH_EXPORT
void kth_core_hash_list_erase(kth_hash_list_mut_t list, kth_size_t index);

KTH_EXPORT
void kth_core_hash_list_nth_out(kth_hash_list_const_t list, kth_size_t n, kth_hash_t* out_hash);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_HASH_LIST_H_ */
