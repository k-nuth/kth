// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CHAIN_TOKEN_KIND_H_
#define KTH_CAPI_CHAIN_TOKEN_KIND_H_

#ifdef __cplusplus
extern "C" {
#endif

// Discriminator for the three variants of the CashTokens payload.
// Mirrors `kth::domain::chain::kind` in the C++ layer so both sides
// share the same integral values.
typedef enum {
    kth_token_kind_fungible_only = 0,
    kth_token_kind_non_fungible_only = 1,
    kth_token_kind_both = 2,
} kth_token_kind_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_TOKEN_KIND_H_ */
