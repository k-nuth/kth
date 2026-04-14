// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_BINARY_H_
#define KTH_CAPI_BINARY_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_binary_mut_t`. Caller must release with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_construct_default(void);

/** @return Owned `kth_binary_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_construct_from_bit_string(char const* bit_string);

/** @return Owned `kth_binary_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_construct_from_size_number(kth_size_t size, uint32_t number);

/** @return Owned `kth_binary_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_construct_from_size_blocks(kth_size_t size, uint8_t const* blocks, kth_size_t n);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_core_binary_destruct(kth_binary_mut_t self);


// Copy

/** @return Owned `kth_binary_mut_t`. Caller must release with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_copy(kth_binary_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_core_binary_equals(kth_binary_const_t self, kth_binary_const_t other);


// Getters

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_core_binary_blocks(kth_binary_const_t self, kth_size_t* out_size);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_core_binary_encoded(kth_binary_const_t self);

KTH_EXPORT
kth_size_t kth_core_binary_size(kth_binary_const_t self);


// Predicates

KTH_EXPORT
kth_bool_t kth_core_binary_is_base2(char const* text);

KTH_EXPORT
kth_bool_t kth_core_binary_is_prefix_of_span(kth_binary_const_t self, uint8_t const* field, kth_size_t n);

KTH_EXPORT
kth_bool_t kth_core_binary_is_prefix_of_uint32(kth_binary_const_t self, uint32_t field);

KTH_EXPORT
kth_bool_t kth_core_binary_is_prefix_of_binary(kth_binary_const_t self, kth_binary_const_t field);


// Operations

KTH_EXPORT
void kth_core_binary_resize(kth_binary_mut_t self, kth_size_t size);

KTH_EXPORT
kth_bool_t kth_core_binary_at(kth_binary_const_t self, kth_size_t index);

KTH_EXPORT
void kth_core_binary_append(kth_binary_mut_t self, kth_binary_const_t post);

KTH_EXPORT
void kth_core_binary_prepend(kth_binary_mut_t self, kth_binary_const_t prior);

KTH_EXPORT
void kth_core_binary_shift_left(kth_binary_mut_t self, kth_size_t distance);

KTH_EXPORT
void kth_core_binary_shift_right(kth_binary_mut_t self, kth_size_t distance);

/** @return Owned `kth_binary_mut_t`, or NULL if construction/parsing fails. Caller must release non-NULL results with `kth_core_binary_destruct`. */
KTH_EXPORT KTH_OWNED
kth_binary_mut_t kth_core_binary_substring(kth_binary_const_t self, kth_size_t start, kth_size_t length);

KTH_EXPORT
kth_bool_t kth_core_binary_less(kth_binary_const_t self, kth_binary_const_t x);


// Static utilities

KTH_EXPORT
kth_size_t kth_core_binary_blocks_size(kth_size_t bit_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_BINARY_H_
