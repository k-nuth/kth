// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_STRING_LIST_H_
#define KTH_CAPI_STRING_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Owned `kth_string_list_mut_t`. Caller must release with `kth_core_string_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_string_list_mut_t kth_core_string_list_construct_default(void);

KTH_EXPORT
void kth_core_string_list_push_back(kth_string_list_mut_t list, char const* elem);

/** No-op if `list` is null. */
KTH_EXPORT
void kth_core_string_list_destruct(kth_string_list_mut_t list);

KTH_EXPORT
kth_size_t kth_core_string_list_count(kth_string_list_const_t list);

/** @return Owned string. Caller must release with `free()`. */
KTH_EXPORT KTH_OWNED
char const* kth_core_string_list_nth(kth_string_list_const_t list, kth_size_t index);

KTH_EXPORT
void kth_core_string_list_assign_at(kth_string_list_mut_t list, kth_size_t index, char const* elem);

KTH_EXPORT
void kth_core_string_list_erase(kth_string_list_mut_t list, kth_size_t index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_STRING_LIST_H_ */
