// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_CASHADDR_H_
#define KTH_CAPI_WALLET_CASHADDR_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_cashaddr_encode(char const* prefix, uint8_t const* payload, kth_size_t n);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_wallet_cashaddr_decode(char const* str, char const* default_prefix, KTH_OUT_OWNED uint8_t** out_payload, kth_size_t* out_payload_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_CASHADDR_H_
