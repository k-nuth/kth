// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_BIG_NUMBER_H_
#define KTH_CAPI_VM_BIG_NUMBER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_construct_default(void);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_construct_from_value(int64_t value);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_construct_from_decimal_str(char const* decimal_str);


// Static factories

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_from_hex(char const* hex_str);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_big_number_destruct(kth_big_number_mut_t self);


// Copy

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_copy(kth_big_number_const_t self);


// Equality

KTH_EXPORT
kth_bool_t kth_vm_big_number_equals(kth_big_number_const_t self, kth_big_number_const_t other);


// Getters

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_big_number_serialize(kth_big_number_const_t self, kth_size_t* out_size);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_vm_big_number_to_string(kth_big_number_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_vm_big_number_to_hex(kth_big_number_const_t self);

KTH_EXPORT
int kth_vm_big_number_sign(kth_big_number_const_t self);

KTH_EXPORT
int32_t kth_vm_big_number_to_int32_saturating(kth_big_number_const_t self);

KTH_EXPORT
kth_size_t kth_vm_big_number_byte_count(kth_big_number_const_t self);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_abs(kth_big_number_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_big_number_data(kth_big_number_const_t self, kth_size_t* out_size);


// Setters

KTH_EXPORT
kth_bool_t kth_vm_big_number_set_data(kth_big_number_mut_t self, uint8_t const* d, kth_size_t n, kth_size_t max_size);


// Predicates

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_zero(kth_big_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_nonzero(kth_big_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_negative(kth_big_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_true(kth_big_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_false(kth_big_number_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_big_number_is_minimally_encoded(uint8_t const* data, kth_size_t n, kth_size_t max_size);


// Operations

KTH_EXPORT
kth_bool_t kth_vm_big_number_deserialize(kth_big_number_mut_t self, uint8_t const* data, kth_size_t n);

KTH_EXPORT
int kth_vm_big_number_compare(kth_big_number_const_t self, kth_big_number_const_t other);

KTH_EXPORT
void kth_vm_big_number_negate(kth_big_number_mut_t self);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_pow(kth_big_number_const_t self, kth_big_number_const_t exp);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_pow_mod(kth_big_number_const_t self, kth_big_number_const_t exp, kth_big_number_const_t mod);

/** @return Owned `kth_big_number_mut_t`. Caller must release with `kth_vm_big_number_destruct`. */
KTH_EXPORT KTH_OWNED
kth_big_number_mut_t kth_vm_big_number_math_modulo(kth_big_number_const_t self, kth_big_number_const_t mod);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_BIG_NUMBER_H_
