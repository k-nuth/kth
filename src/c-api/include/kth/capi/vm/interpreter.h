// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_INTERPRETER_H_
#define KTH_CAPI_VM_INTERPRETER_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

KTH_EXPORT
kth_error_code_t kth_vm_interpreter_run_simple(kth_program_mut_t program);

KTH_EXPORT
kth_error_code_t kth_vm_interpreter_run(kth_operation_const_t op, kth_program_mut_t program);

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_interpreter_debug_begin(kth_program_const_t prog);

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step(kth_debug_snapshot_const_t snapshot);

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step_n(kth_debug_snapshot_const_t snapshot, kth_size_t n);

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step_until(kth_debug_snapshot_const_t snapshot, kth_debug_step_predicate_t predicate, void* predicate_user_data);

/** @return Owned `kth_debug_snapshot_mut_t`. Caller must release with `kth_vm_debug_snapshot_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_mut_t kth_vm_interpreter_debug_run(kth_debug_snapshot_const_t snapshot);

/** @return Owned `kth_debug_snapshot_list_mut_t`. Caller must release with `kth_vm_debug_snapshot_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_debug_snapshot_list_mut_t kth_vm_interpreter_debug_run_traced(kth_debug_snapshot_const_t start);

KTH_EXPORT
kth_error_code_t kth_vm_interpreter_debug_finalize(kth_debug_snapshot_const_t snapshot);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_INTERPRETER_H_
