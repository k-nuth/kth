// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_METRICS_H_
#define KTH_CAPI_VM_METRICS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/chain/script_flags.h>

#ifdef __cplusplus
extern "C" {
#endif

// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_metrics_destruct(kth_metrics_mut_t self);


// Copy

/** @return Owned `kth_metrics_mut_t`. Caller must release with `kth_vm_metrics_destruct`. */
KTH_EXPORT KTH_OWNED
kth_metrics_mut_t kth_vm_metrics_copy(kth_metrics_const_t self);


// Getters

KTH_EXPORT
uint32_t kth_vm_metrics_sig_checks(kth_metrics_const_t self);

KTH_EXPORT
uint64_t kth_vm_metrics_op_cost(kth_metrics_const_t self);

KTH_EXPORT
uint64_t kth_vm_metrics_hash_digest_iterations(kth_metrics_const_t self);


// Setters

KTH_EXPORT
void kth_vm_metrics_set_script_limits(kth_metrics_mut_t self, kth_script_flags_t flags, uint64_t script_sig_size);

KTH_EXPORT
void kth_vm_metrics_set_native_script_limits(kth_metrics_mut_t self, kth_bool_t standard, uint64_t script_sig_size);


// Predicates

KTH_EXPORT
kth_bool_t kth_vm_metrics_is_over_op_cost_limit(kth_metrics_const_t self, kth_script_flags_t flags);

KTH_EXPORT
kth_bool_t kth_vm_metrics_is_over_op_cost_limit_simple(kth_metrics_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_metrics_is_over_hash_iters_limit(kth_metrics_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_metrics_has_valid_script_limits(kth_metrics_const_t self);


// Operations

KTH_EXPORT
void kth_vm_metrics_add_op_cost(kth_metrics_mut_t self, uint32_t cost);

KTH_EXPORT
void kth_vm_metrics_add_push_op(kth_metrics_mut_t self, uint32_t stack_item_length);

KTH_EXPORT
void kth_vm_metrics_add_hash_iterations(kth_metrics_mut_t self, uint32_t message_length, kth_bool_t is_two_round_hash);

KTH_EXPORT
void kth_vm_metrics_add_sig_checks(kth_metrics_mut_t self, uint32_t n_checks);

KTH_EXPORT
uint64_t kth_vm_metrics_composite_op_cost_script_flags(kth_metrics_const_t self, kth_script_flags_t flags);

KTH_EXPORT
uint64_t kth_vm_metrics_composite_op_cost_bool(kth_metrics_const_t self, kth_bool_t standard);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_METRICS_H_
