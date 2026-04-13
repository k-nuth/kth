// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_STEALTH_COMPACT_H_
#define KTH_CAPI_CHAIN_STEALTH_COMPACT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_stealth_compact_destruct(kth_stealth_compact_mut_t self);


// Copy

/** @return Owned `kth_stealth_compact_mut_t`. Caller must release with `kth_chain_stealth_compact_destruct`. */
KTH_EXPORT KTH_OWNED
kth_stealth_compact_mut_t kth_chain_stealth_compact_copy(kth_stealth_compact_const_t self);


// Getters

KTH_EXPORT
kth_hash_t kth_chain_stealth_compact_ephemeral_public_key_hash(kth_stealth_compact_const_t self);

KTH_EXPORT
kth_shorthash_t kth_chain_stealth_compact_public_key_hash(kth_stealth_compact_const_t self);

KTH_EXPORT
kth_hash_t kth_chain_stealth_compact_transaction_hash(kth_stealth_compact_const_t self);


// Setters

KTH_EXPORT
void kth_chain_stealth_compact_set_ephemeral_public_key_hash(kth_stealth_compact_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_stealth_compact_set_ephemeral_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_stealth_compact_set_public_key_hash(kth_stealth_compact_mut_t self, kth_shorthash_t value);

/** @warning `value` MUST point to a buffer of at least 20 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_stealth_compact_set_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value);

KTH_EXPORT
void kth_chain_stealth_compact_set_transaction_hash(kth_stealth_compact_mut_t self, kth_hash_t value);

/** @warning `value` MUST point to a buffer of at least 32 bytes. Passing a shorter buffer is undefined behavior. Prefer the safe variant (without the `_unsafe` suffix) when your language can pass a C struct by value. */
KTH_EXPORT
void kth_chain_stealth_compact_set_transaction_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_STEALTH_COMPACT_H_
