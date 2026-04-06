// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_CAPI_CHAIN_POINT_H_
#define KTH_CAPI_CHAIN_POINT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_point_mut_t kth_chain_point_construct_default(void);

KTH_EXPORT
kth_point_mut_t kth_chain_point_construct(uint8_t const* hash, uint32_t index);

KTH_EXPORT
void kth_chain_point_destruct(kth_point_mut_t point);

KTH_EXPORT
kth_error_code_t kth_chain_point_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_point_mut_t* out_result);

KTH_EXPORT
kth_bool_t kth_chain_point_is_valid(kth_point_const_t point);

KTH_EXPORT
uint8_t const* kth_chain_point_to_data(kth_point_const_t point, kth_bool_t wire, kth_size_t* out_size);

KTH_EXPORT
kth_size_t kth_chain_point_satoshi_fixed_size(void);

KTH_EXPORT
kth_size_t kth_chain_point_serialized_size(kth_point_const_t point, kth_bool_t wire);

KTH_EXPORT
kth_hash_t kth_chain_point_hash(kth_point_const_t point);

KTH_EXPORT
void kth_chain_point_hash_out(kth_point_const_t point, kth_hash_t* out_hash);

KTH_EXPORT
void kth_chain_point_set_hash(kth_point_mut_t point, uint8_t const* hash);

KTH_EXPORT
uint32_t kth_chain_point_index(kth_point_const_t point);

KTH_EXPORT
void kth_chain_point_set_index(kth_point_mut_t point, uint32_t value);

KTH_EXPORT
uint64_t kth_chain_point_checksum(kth_point_const_t point);

KTH_EXPORT
kth_bool_t kth_chain_point_is_null(kth_point_const_t point);

KTH_EXPORT
void kth_chain_point_reset(kth_point_mut_t point);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_POINT_H_ */
