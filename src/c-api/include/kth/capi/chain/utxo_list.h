// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_UTXO_LIST_H_
#define KTH_CAPI_CHAIN_UTXO_LIST_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Owned `kth_utxo_list_mut_t`. Caller must release with `kth_chain_utxo_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_utxo_list_mut_t kth_chain_utxo_list_construct_default(void);

KTH_EXPORT
void kth_chain_utxo_list_push_back(kth_utxo_list_mut_t list, kth_utxo_const_t elem);

/** No-op if `list` is null. */
KTH_EXPORT
void kth_chain_utxo_list_destruct(kth_utxo_list_mut_t list);

KTH_EXPORT
kth_size_t kth_chain_utxo_list_count(kth_utxo_list_const_t list);

/** @return Borrowed `kth_utxo_const_t` view into the list. Do not destruct; the list retains ownership. Invalidated by any mutation of the list. */
KTH_EXPORT
kth_utxo_const_t kth_chain_utxo_list_nth(kth_utxo_list_const_t list, kth_size_t index);

KTH_EXPORT
void kth_chain_utxo_list_assign_at(kth_utxo_list_mut_t list, kth_size_t index, kth_utxo_const_t elem);

KTH_EXPORT
void kth_chain_utxo_list_erase(kth_utxo_list_mut_t list, kth_size_t index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_UTXO_LIST_H_ */
