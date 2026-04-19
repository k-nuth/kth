// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/interpreter.h>

#include <kth/capi/helpers.hpp>
#include <kth/capi/conversions.hpp>

#include <kth/domain/machine/program.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_error_code_t kth_vm_interpreter_run(kth_program_mut_t program) {
    auto const result = kth::domain::machine::interpreter::run(
        kth::cpp_ref<kth::domain::machine::program>(program));
    return kth::to_c_err(result.error);
}

kth_error_code_t kth_vm_interpreter_run_operation(kth_operation_t operation, kth_program_mut_t program) {
    auto const result = kth::domain::machine::interpreter::run(
        kth::cpp_ref<kth::domain::machine::operation>(operation),
        kth::cpp_ref<kth::domain::machine::program>(program)
    );
    return kth::to_c_err(result.error);
}


// Debug-session bindings: the underlying C++ API was reshaped around
// a value `debug_snapshot` that carries the program + last result +
// done flag. The handle-based shims below stay in place just to keep
// this translation unit compiling; they will be replaced with the
// real opaque-handle bindings in a follow-up.

kth_error_code_t kth_vm_interpreter_debug_start(kth_program_const_t program, kth_size_t* out_step) {
    (void)program;
    if (out_step != nullptr) *out_step = 0;
    return kth::to_c_err(kth::error::not_implemented);
}

kth_bool_t kth_vm_interpreter_debug_steps_available(kth_program_const_t program, kth_size_t step) {
    (void)program;
    (void)step;
    return 0;
}

kth_error_code_t kth_vm_interpreter_debug_step(kth_program_const_t program, kth_size_t step, kth_size_t* out_step, kth_program_mut_t* out_program) {
    (void)program;
    (void)step;
    if (out_step != nullptr) *out_step = 0;
    if (out_program != nullptr) *out_program = nullptr;
    return kth::to_c_err(kth::error::not_implemented);
}

kth_error_code_t kth_vm_interpreter_debug_end(kth_program_const_t program) {
    (void)program;
    return kth::to_c_err(kth::error::not_implemented);
}

} // extern "C"
