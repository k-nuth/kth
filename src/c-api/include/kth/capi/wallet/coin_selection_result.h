// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_COIN_SELECTION_RESULT_H_
#define KTH_CAPI_WALLET_COIN_SELECTION_RESULT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_wallet_coin_selection_result_destruct(kth_coin_selection_result_mut_t self);


// Copy

/** @return Owned `kth_coin_selection_result_mut_t`. Caller must release with `kth_wallet_coin_selection_result_destruct`. */
KTH_EXPORT KTH_OWNED
kth_coin_selection_result_mut_t kth_wallet_coin_selection_result_copy(kth_coin_selection_result_const_t self);


// Getters

KTH_EXPORT
uint64_t kth_wallet_coin_selection_result_total_selected_bch(kth_coin_selection_result_const_t self);

KTH_EXPORT
uint64_t kth_wallet_coin_selection_result_total_selected_token(kth_coin_selection_result_const_t self);

KTH_EXPORT
uint64_t kth_wallet_coin_selection_result_estimated_size(kth_coin_selection_result_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_COIN_SELECTION_RESULT_H_
