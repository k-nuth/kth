// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_HISTORY_COMPACT_H_
#define KTH_CAPI_CHAIN_HISTORY_COMPACT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_chain_history_compact_destruct(kth_history_compact_mut_t self);


// Copy

/** @return Owned `kth_history_compact_mut_t`. Caller must release with `kth_chain_history_compact_destruct`. */
KTH_EXPORT KTH_OWNED
kth_history_compact_mut_t kth_chain_history_compact_copy(kth_history_compact_const_t self);


// Getters

KTH_EXPORT
kth_point_kind_t kth_chain_history_compact_kind(kth_history_compact_const_t self);

/** @return Borrowed `kth_point_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_point_const_t kth_chain_history_compact_point(kth_history_compact_const_t self);

KTH_EXPORT
uint32_t kth_chain_history_compact_height(kth_history_compact_const_t self);

KTH_EXPORT
uint64_t kth_chain_history_compact_value_or_previous_checksum(kth_history_compact_const_t self);


// Setters

KTH_EXPORT
void kth_chain_history_compact_set_kind(kth_history_compact_mut_t self, kth_point_kind_t value);

/** @param value Borrowed input. Copied by value into the resulting object; ownership of `value` stays with the caller. */
KTH_EXPORT
void kth_chain_history_compact_set_point(kth_history_compact_mut_t self, kth_point_const_t value);

KTH_EXPORT
void kth_chain_history_compact_set_height(kth_history_compact_mut_t self, uint32_t value);

KTH_EXPORT
void kth_chain_history_compact_set_value_or_previous_checksum(kth_history_compact_mut_t self, uint64_t value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_CHAIN_HISTORY_COMPACT_H_
