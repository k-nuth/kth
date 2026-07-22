// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_CHAIN_QUERY_H_
#define KTH_CAPI_CHAIN_CHAIN_QUERY_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/callbacks.h>

#ifdef __cplusplus
extern "C" {
#endif

// Operations

/** @param hash Borrowed input; must be non-null. Read during the call; ownership of `hash` stays with the caller. */
KTH_EXPORT
kth_error_code_t kth_chain_sync_block_height(kth_chain_t self, kth_hash_t const* hash, kth_size_t* out);

/** @warning `hash` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`. */
KTH_EXPORT
kth_error_code_t kth_chain_sync_block_height_unsafe(kth_chain_t self, uint8_t const* hash, kth_size_t* out);

/** @param hash Borrowed input; must be non-null. Read during the call; ownership of `hash` stays with the caller. */
KTH_EXPORT
void kth_chain_async_block_height(kth_chain_t self, void* ctx, kth_hash_t const* hash, kth_block_height_fetch_handler_t handler);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_CHAIN_QUERY_H_
