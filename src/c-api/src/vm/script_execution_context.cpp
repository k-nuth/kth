// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/script_execution_context.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/machine/script_execution_context.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::script_execution_context;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_script_execution_context_mut_t kth_vm_script_execution_context_construct(uint32_t input_index, kth_transaction_const_t transaction) {
    KTH_PRECONDITION(transaction != nullptr);
    auto const& transaction_cpp = kth::cpp_ref<kth::domain::chain::transaction>(transaction);
    return kth::leak<cpp_t>(input_index, transaction_cpp);
}


// Destructor

void kth_vm_script_execution_context_destruct(kth_script_execution_context_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_script_execution_context_mut_t kth_vm_script_execution_context_copy(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

uint32_t kth_vm_script_execution_context_input_index(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).input_index();
}

kth_transaction_const_t kth_vm_script_execution_context_transaction(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).transaction());
}

uint32_t kth_vm_script_execution_context_input_count(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).input_count();
}

uint32_t kth_vm_script_execution_context_output_count(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).output_count();
}

uint32_t kth_vm_script_execution_context_tx_version(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).tx_version();
}

uint32_t kth_vm_script_execution_context_tx_locktime(kth_script_execution_context_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).tx_locktime();
}

} // extern "C"
