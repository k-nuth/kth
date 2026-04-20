// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_DEBUG_SNAPSHOT_H_
#define KTH_CAPI_VM_DEBUG_SNAPSHOT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_debug_snapshot_construct_default(void);

/**
 * @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`.
 * @param p Borrowed input. Copied by value into the resulting object; ownership of `p` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_debug_snapshot_construct(kth_program_const_t p);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_debug_snapshot_destruct(kth_debug_snapshot_mut_t self);


// Copy

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_debug_snapshot_copy(kth_debug_snapshot_const_t self);


// Getters

/** @return Borrowed `kth_program_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_program_const_t kth_vm_debug_snapshot_prog(kth_debug_snapshot_const_t self);

KTH_EXPORT
kth_size_t kth_vm_debug_snapshot_step(kth_debug_snapshot_const_t self);

KTH_EXPORT
kth_error_code_t kth_vm_debug_snapshot_last(kth_debug_snapshot_const_t self);

KTH_EXPORT
kth_bool_t kth_vm_debug_snapshot_done(kth_debug_snapshot_const_t self);

/** @return Borrowed `kth_bool_list_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_bool_list_const_t kth_vm_debug_snapshot_control_stack(kth_debug_snapshot_const_t self);

/** @return Owned `kth_u64_list_mut_t`. Caller must release with `kth_core_u64_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_u64_list_mut_t kth_vm_debug_snapshot_loop_stack(kth_debug_snapshot_const_t self);

/** @return Borrowed `kth_function_table_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_function_table_const_t kth_vm_debug_snapshot_function_table(kth_debug_snapshot_const_t self);

KTH_EXPORT
kth_size_t kth_vm_debug_snapshot_invoke_depth(kth_debug_snapshot_const_t self);

KTH_EXPORT
kth_size_t kth_vm_debug_snapshot_outer_loop_depth(kth_debug_snapshot_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_DEBUG_SNAPSHOT_H_
