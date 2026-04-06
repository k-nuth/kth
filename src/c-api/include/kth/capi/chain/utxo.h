// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_CHAIN_UTXO_H_
#define KTH_CAPI_CHAIN_UTXO_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_utxo_mut_t kth_chain_utxo_construct_default(void);

KTH_EXPORT
void kth_chain_utxo_destruct(kth_utxo_mut_t utxo);

KTH_EXPORT
uint32_t kth_chain_utxo_height(kth_utxo_const_t utxo);

KTH_EXPORT
kth_output_point_const_t kth_chain_utxo_point(kth_utxo_const_t utxo);

KTH_EXPORT
uint64_t kth_chain_utxo_amount(kth_utxo_const_t utxo);

KTH_EXPORT
kth_token_data_const_t kth_chain_utxo_token_data(kth_utxo_const_t utxo);

KTH_EXPORT
void kth_chain_utxo_set_height(kth_utxo_mut_t utxo, uint32_t height);

KTH_EXPORT
void kth_chain_utxo_set_point(kth_utxo_mut_t utxo, kth_output_point_const_t point);

KTH_EXPORT
void kth_chain_utxo_set_amount(kth_utxo_mut_t utxo, uint64_t amount);

KTH_EXPORT
void kth_chain_utxo_set_token_data(kth_utxo_mut_t utxo, kth_token_data_const_t token_data);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_UTXO_H_ */
