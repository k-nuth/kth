// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/debug_snapshot.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/machine/interpreter.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::debug_snapshot;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_debug_snapshot_mut_t kth_vm_debug_snapshot_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_debug_snapshot_mut_t kth_vm_debug_snapshot_construct(kth_program_const_t p) {
    KTH_PRECONDITION(p != nullptr);
    auto const& p_cpp = kth::cpp_ref<kth::domain::machine::program>(p);
    return kth::leak<cpp_t>(p_cpp);
}


// Destructor

void kth_vm_debug_snapshot_destruct(kth_debug_snapshot_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_debug_snapshot_mut_t kth_vm_debug_snapshot_copy(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_program_const_t kth_vm_debug_snapshot_prog(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).prog);
}

kth_size_t kth_vm_debug_snapshot_step(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).step;
}

kth_error_code_t kth_vm_debug_snapshot_last(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).last.error);
}

kth_bool_t kth_vm_debug_snapshot_done(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).done);
}

kth_bool_list_const_t kth_vm_debug_snapshot_control_stack(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).control_stack);
}

kth_u64_list_mut_t kth_vm_debug_snapshot_loop_stack(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const cpp_result = kth::cpp_ref<cpp_t>(self).loop_stack;
    return kth::leak_list<uint64_t>(cpp_result.begin(), cpp_result.end());
}

kth_function_table_const_t kth_vm_debug_snapshot_function_table(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).function_table);
}

kth_size_t kth_vm_debug_snapshot_invoke_depth(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).invoke_depth;
}

kth_size_t kth_vm_debug_snapshot_outer_loop_depth(kth_debug_snapshot_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).outer_loop_depth;
}

} // extern "C"
