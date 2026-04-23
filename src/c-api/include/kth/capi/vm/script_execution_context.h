// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_SCRIPT_EXECUTION_CONTEXT_H_
#define KTH_CAPI_VM_SCRIPT_EXECUTION_CONTEXT_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/**
 * @return Owned `kth_script_execution_context_mut_t`. Caller must release with `kth_vm_script_execution_context_destruct`.
 * @param transaction Borrowed input. Copied by value into the resulting object; ownership of `transaction` stays with the caller.
 */
KTH_EXPORT KTH_OWNED
kth_script_execution_context_mut_t kth_vm_script_execution_context_construct(uint32_t input_index, kth_transaction_const_t transaction);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_vm_script_execution_context_destruct(kth_script_execution_context_mut_t self);


// Copy

/** @return Owned `kth_script_execution_context_mut_t`. Caller must release with `kth_vm_script_execution_context_destruct`. */
KTH_EXPORT KTH_OWNED
kth_script_execution_context_mut_t kth_vm_script_execution_context_copy(kth_script_execution_context_const_t self);


// Getters

KTH_EXPORT
uint32_t kth_vm_script_execution_context_input_index(kth_script_execution_context_const_t self);

/** @return Borrowed `kth_transaction_const_t` view into `self`. Do not destruct; the parent object retains ownership. Invalidated by any mutation of `self`. */
KTH_EXPORT
kth_transaction_const_t kth_vm_script_execution_context_transaction(kth_script_execution_context_const_t self);

KTH_EXPORT
uint32_t kth_vm_script_execution_context_input_count(kth_script_execution_context_const_t self);

KTH_EXPORT
uint32_t kth_vm_script_execution_context_output_count(kth_script_execution_context_const_t self);

KTH_EXPORT
uint32_t kth_vm_script_execution_context_tx_version(kth_script_execution_context_const_t self);

KTH_EXPORT
uint32_t kth_vm_script_execution_context_tx_locktime(kth_script_execution_context_const_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_VM_SCRIPT_EXECUTION_CONTEXT_H_
