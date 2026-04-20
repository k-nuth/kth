// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/interpreter.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/machine/interpreter.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::interpreter;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_error_code_t kth_vm_interpreter_run_simple(kth_program_mut_t program) {
    KTH_PRECONDITION(program != nullptr);
    auto& program_cpp = kth::cpp_ref<kth::domain::machine::program>(program);
    return kth::to_c_err(cpp_t::run(program_cpp).error);
}

kth_error_code_t kth_vm_interpreter_run(kth_operation_const_t op, kth_program_mut_t program) {
    KTH_PRECONDITION(op != nullptr);
    KTH_PRECONDITION(program != nullptr);
    auto const& op_cpp = kth::cpp_ref<kth::domain::machine::operation>(op);
    auto& program_cpp = kth::cpp_ref<kth::domain::machine::program>(program);
    return kth::to_c_err(cpp_t::run(op_cpp, program_cpp).error);
}

kth_debug_snapshot_mut_t kth_vm_interpreter_debug_begin(kth_program_const_t prog) {
    KTH_PRECONDITION(prog != nullptr);
    auto const& prog_cpp = kth::cpp_ref<kth::domain::machine::program>(prog);
    return kth::leak(cpp_t::debug_begin(prog_cpp));
}

kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step(kth_debug_snapshot_const_t snapshot) {
    KTH_PRECONDITION(snapshot != nullptr);
    auto const& snapshot_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(snapshot);
    return kth::leak(cpp_t::debug_step(snapshot_cpp));
}

kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step_n(kth_debug_snapshot_const_t snapshot, kth_size_t n) {
    KTH_PRECONDITION(snapshot != nullptr);
    auto const& snapshot_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(snapshot);
    auto const n_cpp = kth::sz(n);
    return kth::leak(cpp_t::debug_step_n(snapshot_cpp, n_cpp));
}

kth_debug_snapshot_mut_t kth_vm_interpreter_debug_step_until(kth_debug_snapshot_const_t snapshot, kth_debug_step_predicate_t predicate, void* predicate_user_data) {
    KTH_PRECONDITION(snapshot != nullptr);
    KTH_PRECONDITION(predicate != nullptr);
    auto const& snapshot_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(snapshot);
    auto const predicate_cpp = [predicate, predicate_user_data](kth::domain::machine::debug_snapshot const& snap) -> bool { return predicate(&snap, predicate_user_data) != 0; };
    return kth::leak(cpp_t::debug_step_until(snapshot_cpp, predicate_cpp));
}

kth_debug_snapshot_mut_t kth_vm_interpreter_debug_run(kth_debug_snapshot_const_t snapshot) {
    KTH_PRECONDITION(snapshot != nullptr);
    auto const& snapshot_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(snapshot);
    return kth::leak(cpp_t::debug_run(snapshot_cpp));
}

kth_debug_snapshot_list_mut_t kth_vm_interpreter_debug_run_traced(kth_debug_snapshot_const_t start) {
    KTH_PRECONDITION(start != nullptr);
    auto const& start_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(start);
    return kth::leak_list<kth::domain::machine::debug_snapshot>(cpp_t::debug_run_traced(start_cpp));
}

kth_error_code_t kth_vm_interpreter_debug_finalize(kth_debug_snapshot_const_t snapshot) {
    KTH_PRECONDITION(snapshot != nullptr);
    auto const& snapshot_cpp = kth::cpp_ref<kth::domain::machine::debug_snapshot>(snapshot);
    return kth::to_c_err(cpp_t::debug_finalize(snapshot_cpp).error);
}

} // extern "C"
