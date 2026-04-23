// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_PROGRAM_H_
#define KTH_CAPI_VM_PROGRAM_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`. */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_default(void);

/**
 * @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_from_script(kth_script_const_t script);

/**
 * @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 * @param transaction Borrowed input. Copied by value into the resulting object; ownership of `transaction` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_from_script_transaction_input_index_flags_value(kth_script_const_t script, kth_transaction_const_t transaction, uint32_t input_index, kth_script_flags_t flags, uint64_t value);

/**
 * @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 * @param transaction Borrowed input. Copied by value into the resulting object; ownership of `transaction` stays with the caller.
 * @param stack Borrowed input. Copied by value into the resulting object; ownership of `stack` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_from_script_transaction_input_index_flags_stack_value(kth_script_const_t script, kth_transaction_const_t transaction, uint32_t input_index, kth_script_flags_t flags, kth_data_stack_const_t stack, uint64_t value);

/**
 * @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 * @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_from_script_program(kth_script_const_t script, kth_program_const_t x);

/**
 * @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`.
 * @param script Borrowed input. Copied by value into the resulting object; ownership of `script` stays with the caller.
 * @param x Borrowed input. Copied by value into the resulting object; ownership of `x` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_construct_from_script_program_move(kth_script_const_t script, kth_program_const_t x, kth_bool_t move);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_program_destruct(kth_program_mut_t self);


// Copy

/** @return Owned `kth_program_mut_t`. Caller must release with `kth_vm_program_destruct`. */
KTH_EXPORT KTH_OWNED
kth_program_mut_t kth_vm_program_copy(kth_program_const_t self);


// Getters

/** @return Borrowed `kth_metrics_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_metrics_const_t kth_vm_program_get_metrics(kth_program_const_t self);

KTH_EXPORT
kth_script_flags_t kth_vm_program_flags(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_max_script_element_size(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_max_integer_size_legacy(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_max_integer_size(kth_program_const_t self);

KTH_EXPORT
uint32_t kth_vm_program_input_index(kth_program_const_t self);

KTH_EXPORT
uint64_t kth_vm_program_value(kth_program_const_t self);

/** @return Borrowed `kth_transaction_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_transaction_const_t kth_vm_program_transaction(kth_program_const_t self);

/** @return Borrowed `kth_script_execution_context_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_script_execution_context_const_t kth_vm_program_context(kth_program_const_t self);

/** @return Borrowed `kth_script_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_script_const_t kth_vm_program_get_script(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_operation_count(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_empty(kth_program_const_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_program_top(kth_program_const_t self, kth_size_t* out_size);

/** @return Owned `kth_operation_list_mut_t`. Caller must release with `kth_chain_operation_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_operation_list_mut_t kth_vm_program_subscript(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_size(kth_program_const_t self);

KTH_EXPORT
kth_size_t kth_vm_program_conditional_stack_size(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_empty_alternate(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_closed(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_succeeded(kth_program_const_t self);


// Predicates

KTH_EXPORT
kth_bool_t kth_vm_program_is_valid(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_is_chip_vm_limits_enabled(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_is_bigint_enabled(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_is_stack_overflow_simple(kth_program_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_program_is_stack_overflow(kth_program_const_t self, kth_size_t extra);


// Operations

KTH_EXPORT
void kth_vm_program_reset_active_script(kth_program_mut_t self);

KTH_EXPORT
kth_error_code_t kth_vm_program_evaluate_simple(kth_program_mut_t self);

KTH_EXPORT
kth_error_code_t kth_vm_program_evaluate(kth_program_mut_t self, kth_operation_const_t op);

KTH_EXPORT
kth_bool_t kth_vm_program_increment_operation_count_operation(kth_program_mut_t self, kth_operation_const_t op);

KTH_EXPORT
kth_bool_t kth_vm_program_increment_operation_count_int32(kth_program_mut_t self, int32_t public_keys);

KTH_EXPORT
kth_bool_t kth_vm_program_mark_code_separator(kth_program_mut_t self, kth_size_t pc);

KTH_EXPORT
void kth_vm_program_push(kth_program_mut_t self, kth_bool_t value);

KTH_EXPORT
void kth_vm_program_push_move(kth_program_mut_t self, uint8_t const* item, kth_size_t n);

KTH_EXPORT
void kth_vm_program_push_copy(kth_program_mut_t self, uint8_t const* item, kth_size_t n);

KTH_EXPORT
void kth_vm_program_drop(kth_program_mut_t self);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_program_pop_simple(kth_program_mut_t self, kth_size_t* out_size);

KTH_EXPORT
kth_error_code_t kth_vm_program_pop_int32(kth_program_mut_t self, int32_t* out);

KTH_EXPORT
kth_error_code_t kth_vm_program_pop_int64(kth_program_mut_t self, int64_t* out);

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_number(kth_program_mut_t self, kth_size_t maximum_size, KTH_OUT_OWNED kth_number_mut_t* out);

/**
 * @param[out] out_first Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error.
 * @param[out] out_second Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_binary(kth_program_mut_t self, KTH_OUT_OWNED kth_number_mut_t* out_first, KTH_OUT_OWNED kth_number_mut_t* out_second);

/**
 * @param[out] out_0 Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error.
 * @param[out] out_1 Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error.
 * @param[out] out_2 Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_ternary(kth_program_mut_t self, KTH_OUT_OWNED kth_number_mut_t* out_0, KTH_OUT_OWNED kth_number_mut_t* out_1, KTH_OUT_OWNED kth_number_mut_t* out_2);

/** @param[out] out Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_big_number(kth_program_mut_t self, kth_size_t maximum_size, KTH_OUT_OWNED kth_big_number_mut_t* out);

/**
 * @param[out] out_first Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error.
 * @param[out] out_second Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_big_binary(kth_program_mut_t self, KTH_OUT_OWNED kth_big_number_mut_t* out_first, KTH_OUT_OWNED kth_big_number_mut_t* out_second);

/**
 * @param[out] out_0 Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error.
 * @param[out] out_1 Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error.
 * @param[out] out_2 Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop_big_ternary(kth_program_mut_t self, KTH_OUT_OWNED kth_big_number_mut_t* out_0, KTH_OUT_OWNED kth_big_number_mut_t* out_1, KTH_OUT_OWNED kth_big_number_mut_t* out_2);

KTH_EXPORT
kth_error_code_t kth_vm_program_pop_index(kth_program_mut_t self, uint32_t* out);

/** @param[out] out Must point to a null `kth_data_stack_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_chain_data_stack_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_program_pop(kth_program_mut_t self, kth_size_t count, KTH_OUT_OWNED kth_data_stack_mut_t* out);

KTH_EXPORT
void kth_vm_program_duplicate(kth_program_mut_t self, kth_size_t index);

KTH_EXPORT
void kth_vm_program_swap(kth_program_mut_t self, kth_size_t index_left, kth_size_t index_right);

KTH_EXPORT
kth_bool_t kth_vm_program_stack_true(kth_program_const_t self, kth_bool_t clean);

KTH_EXPORT
kth_bool_t kth_vm_program_stack_result(kth_program_const_t self, kth_bool_t clean);

KTH_EXPORT
kth_bool_t kth_vm_program_if_(kth_program_const_t self, kth_operation_const_t op);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_program_item(kth_program_const_t self, kth_size_t index, kth_size_t* out_size);

/** @param[out] out Must point to a null `kth_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_program_top_number(kth_program_const_t self, kth_size_t maximum_size, KTH_OUT_OWNED kth_number_mut_t* out);

/** @param[out] out Must point to a null `kth_big_number_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_vm_big_number_destruct`. Untouched on error. */
KTH_EXPORT
kth_error_code_t kth_vm_program_top_big_number(kth_program_const_t self, kth_size_t maximum_size, KTH_OUT_OWNED kth_big_number_mut_t* out);

KTH_EXPORT
void kth_vm_program_push_alternate(kth_program_mut_t self, uint8_t const* value, kth_size_t n);

/** @return Owned byte buffer. Caller must release with `kth_core_destruct_array` (length is written to `out_size`). */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_program_pop_alternate(kth_program_mut_t self, kth_size_t* out_size);

KTH_EXPORT
void kth_vm_program_open(kth_program_mut_t self, kth_bool_t value);

KTH_EXPORT
void kth_vm_program_negate(kth_program_mut_t self);

KTH_EXPORT
void kth_vm_program_close(kth_program_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_PROGRAM_H_
