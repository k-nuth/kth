// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_NUMBER_H_
#define KTH_CAPI_VM_NUMBER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_construct_default(void);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_number_destruct(kth_number_mut_t self);


// Copy

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_copy(kth_number_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_vm_number_equals(kth_number_const_t self, kth_number_const_t other);


// Getters

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_number_data(kth_number_const_t self, kth_size_t* out_size);

KTH_EXPORT
int32_t kth_vm_number_int32(kth_number_const_t self);

KTH_EXPORT
int64_t kth_vm_number_int64(kth_number_const_t self);


// Setters

KTH_EXPORT
kth_bool_t kth_vm_number_set_data(kth_number_mut_t self, uint8_t const* data, kth_size_t n, kth_size_t max_size);


// Predicates

KTH_EXPORT
kth_bool_t kth_vm_number_is_true(kth_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_number_is_false(kth_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_number_is_minimally_encoded(uint8_t const* data, kth_size_t n, kth_size_t max_integer_size);


// Operations

KTH_EXPORT
kth_bool_t kth_vm_number_valid(kth_number_mut_t self, kth_size_t max_size);

KTH_EXPORT
kth_bool_t kth_vm_number_greater(kth_number_const_t self, int64_t value);

KTH_EXPORT
kth_bool_t kth_vm_number_less(kth_number_const_t self, int64_t value);

KTH_EXPORT
kth_bool_t kth_vm_number_greater_or_equal(kth_number_const_t self, int64_t value);

KTH_EXPORT
kth_bool_t kth_vm_number_less_or_equal(kth_number_const_t self, int64_t value);

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_add_int64(kth_number_const_t self, int64_t value);

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_subtract_int64(kth_number_const_t self, int64_t value);

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_add_number(kth_number_const_t self, kth_number_const_t x);

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_subtract_number(kth_number_const_t self, kth_number_const_t x);

/** @return Owned `kth_number_mut_t`. Caller must release with `kth_vm_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_number_mut_t kth_vm_number_multiply(kth_number_const_t self, kth_number_const_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_add_number(kth_number_mut_t self, kth_number_const_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_add_int64(kth_number_mut_t self, int64_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_sub_number(kth_number_mut_t self, kth_number_const_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_sub_int64(kth_number_mut_t self, int64_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_mul_number(kth_number_mut_t self, kth_number_const_t x);

KTH_EXPORT
kth_bool_t kth_vm_number_safe_mul_int64(kth_number_mut_t self, int64_t x);


// Static utilities

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_number_from_int(int64_t value, KTH_OUT_OWNED kth_number_mut_t* out);

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_number_safe_add_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out);

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_number_safe_sub_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out);

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_number_safe_mul_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out);

KTH_EXPORT
kth_size_t kth_vm_number_minimally_encode(uint8_t* data, kth_size_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_NUMBER_H_
