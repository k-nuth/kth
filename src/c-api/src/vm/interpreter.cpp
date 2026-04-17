// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/interpreter.h>

#include <kth/capi/helpers.hpp>
#include <kth/capi/conversions.hpp>

#include <kth/domain/machine/program.hpp>

// ---------------------------------------------------------------------------
extern "C" {



/// Run program script.

    // static
    // code run(program& program);

    // /// Run individual operations (idependent of the script).
    // /// For best performance use script runner for a sequence of operations.
    // static
    // code run(operation const& op, program& program);


kth_error_code_t kth_vm_interpreter_run(kth_program_mut_t program) {
    auto const result = kth::domain::machine::interpreter::run(kth::cpp_ref<kth::domain::machine::program>(program));
    return kth::to_c_err(result);
}

kth_error_code_t kth_vm_interpreter_run_operation(kth_operation_t operation, kth_program_mut_t program) {
    auto const result = kth::domain::machine::interpreter::run(
        kth::cpp_ref<kth::domain::machine::operation>(operation),
        kth::cpp_ref<kth::domain::machine::program>(program)
    );
    return kth::to_c_err(result);
}



    // static
    // std::pair<code, size_t> debug_start(program const& program);

    // static
    // bool debug_steps_available(program const& program, size_t step);

    // static
    // std::tuple<code, size_t, program> debug_step(program program, size_t step);

    // static
    // code debug_end(program const& program);


kth_error_code_t kth_vm_interpreter_debug_start(kth_program_const_t program, kth_size_t* out_step) {
    auto const result = kth::domain::machine::interpreter::debug_start(kth::cpp_ref<kth::domain::machine::program>(program));
    *out_step = result.second;
    return kth::to_c_err(result.first);
}

kth_bool_t kth_vm_interpreter_debug_steps_available(kth_program_const_t program, kth_size_t step) {
    return kth::domain::machine::interpreter::debug_steps_available(kth::cpp_ref<kth::domain::machine::program>(program), step);
}

kth_error_code_t kth_vm_interpreter_debug_step(kth_program_const_t program, kth_size_t step, kth_size_t* out_step, kth_program_mut_t* out_program) {
    auto [err, new_step, new_program_cpp] = kth::domain::machine::interpreter::debug_step(kth::cpp_ref<kth::domain::machine::program>(program), step);
    *out_step = new_step;
    *out_program = kth::leak(std::move(new_program_cpp));
    // printf("kth_vm_interpreter_debug_step() - out_step:     %p\n", out_step);
    // printf("kth_vm_interpreter_debug_step() - out_program:  %p\n", out_program);
    // printf("kth_vm_interpreter_debug_step() - *out_program: %p\n", *out_program);
    return kth::to_c_err(err);
}

kth_error_code_t kth_vm_interpreter_debug_end(kth_program_const_t program) {
    return kth::to_c_err(kth::domain::machine::interpreter::debug_end(kth::cpp_ref<kth::domain::machine::program>(program)));
}

} // extern "C"
