// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CHAIN_TOKEN_CAPABILITY_H_
#define KTH_CAPI_CHAIN_TOKEN_CAPABILITY_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kth_token_capability_none = 0x00,  // No capability: either fungible-only or immutable NFT.
    kth_token_capability_mut = 0x01,  // Mutable NFT: the payload can be altered.
    kth_token_capability_minting = 0x02,  // Minting NFT: used to mint new tokens of the category.
} kth_token_capability_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_TOKEN_CAPABILITY_H_ */
