// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_CHAIN_MEMPOOL_H_
#define KTH_CAPI_CHAIN_CHAIN_MEMPOOL_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/mempool_entry_info.h>
#include <kth/capi/chain/mempool_totals.h>

#ifdef __cplusplus
extern "C" {
#endif

// Getters

/** @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_txids(kth_chain_t self);

KTH_EXPORT
kth_mempool_totals_t kth_chain_get_mempool_info(kth_chain_t self);


// Operations

/** @param txid Borrowed input; must be non-null. Read during the call; ownership of `txid` stays with the caller. */
KTH_EXPORT
kth_bool_t kth_chain_get_mempool_entry(kth_chain_t self, kth_hash_t const* txid, kth_mempool_entry_info_t* out);

/** @warning `txid` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`. */
KTH_EXPORT
kth_bool_t kth_chain_get_mempool_entry_unsafe(kth_chain_t self, uint8_t const* txid, kth_mempool_entry_info_t* out);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @param txid Borrowed input; must be non-null. Read during the call; ownership of `txid` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_depends(kth_chain_t self, kth_hash_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @warning `txid` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_depends_unsafe(kth_chain_t self, uint8_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @param txid Borrowed input; must be non-null. Read during the call; ownership of `txid` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_spentby(kth_chain_t self, kth_hash_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @warning `txid` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_spentby_unsafe(kth_chain_t self, uint8_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @param txid Borrowed input; must be non-null. Read during the call; ownership of `txid` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_ancestors(kth_chain_t self, kth_hash_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @warning `txid` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_ancestors_unsafe(kth_chain_t self, uint8_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @param txid Borrowed input; must be non-null. Read during the call; ownership of `txid` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_descendants(kth_chain_t self, kth_hash_t const* txid);

/**
 * @return Owned `kth_hash_list_mut_t`. Caller must release with `kth_core_hash_list_destruct`.
 * @warning `txid` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a pointer to `kth_hash_t`.
 */
KTH_EXPORT KTH_OWNED
kth_hash_list_mut_t kth_chain_get_mempool_descendants_unsafe(kth_chain_t self, uint8_t const* txid);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_CHAIN_MEMPOOL_H_
