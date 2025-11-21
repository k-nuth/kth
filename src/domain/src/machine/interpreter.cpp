// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/interpreter.hpp>

// #include <kth/domain/constants.hpp>
// #include <kth/domain/machine/operation.hpp>
// #include <kth/domain/machine/program.hpp>
// #include <kth/infrastructure/error.hpp>

namespace kth::domain::machine {

code interpreter::run(program& program) {
    code ec;

    if ( ! program.is_valid()) {
        return error::invalid_script;
    }

    // BCHN code:
    // bool const chipVmLimitsEnabled = (flags & SCRIPT_ENABLE_MAY2025) != 0;
    // size_t const maxScriptElementSize = chipVmLimitsEnabled ? may2025::MAX_SCRIPT_ELEMENT_SIZE
    //                                                         : MAX_SCRIPT_ELEMENT_SIZE_LEGACY;

    // if (chipVmLimitsEnabled && !metrics.HasValidScriptLimits() && context) {
    //     // Calculate metrics "scriptLimits", if not already passed-in, and if we have a `context` object
    //     // from which to get the scriptSig size.
    //     metrics.SetScriptLimits(flags, context->scriptSig().size());
    // }

    // TODO: flags vs forks
    // if (program.is_chip_vm_limits_enabled() && !program.get_metrics().has_valid_script_limits() && context) {
    //     // Calculate metrics "scriptLimits", if not already passed-in, and if we have a `context` object
    //     // from which to get the scriptSig size.
    //     program.get_metrics().set_script_limits(program.get_flags(), context->scriptSig().size());
    // }

    for (auto const& op : program) {
        if (op.is_oversized(program.max_script_element_size())) {
            return error::invalid_push_data_size;
        }

        if (op.is_disabled(program.forks())) {
            return error::op_disabled;
        }

        if ( ! program.increment_operation_count(op)) {
            return error::invalid_operation_count;
        }

        if (program.if_(op)) {
            if ((ec = run_op(op, program))) {
                return ec;
            }

            if (program.is_stack_overflow()) {
                return error::invalid_stack_size;
            }

            // Enforce May 2025 VM limits
            if (program.is_chip_vm_limits_enabled()) {
                // // Check that this opcode did not cause us to exceed opCost and/or hashIters limits.
                // // Note: `metrics` may lack a valid "scriptLimits" object in rare cases (tests only), in which case
                // // the below two limit checks are always going to return false.

                // if (metrics.IsOverOpCostLimit(flags)) {
                //     return set_error(serror, ScriptError::OP_COST);
                // }
                // if (metrics.IsOverHashItersLimit()) {
                //     return set_error(serror, ScriptError::TOO_MANY_HASH_ITERS);
                // }

                // // Conditional stack may not exceed depth of 100
                // if (vfExec.size() > may2025::MAX_CONDITIONAL_STACK_DEPTH) {
                //     return set_error(serror, ScriptError::CONDITIONAL_STACK_DEPTH);
                // }


                // TODO: flags vs forks
                // if (program.get_metrics().is_over_op_cost_limit(program.get_flags())) {
                //     return error::op_cost;
                // }

                if (program.get_metrics().is_over_hash_iters_limit()) {
                    return error::too_many_hash_iters;
                }

                // Conditional stack may not exceed depth of 100
                if (program.conditional_stack_size() > ::kth::may2025::max_conditional_stack_depth) {
                    return error::conditional_stack_depth;
                }
            }
        }
    }

    return program.closed() ? error::success : error::invalid_stack_scope;
}

code interpreter::run(operation const& op, program& program) {
    return run_op(op, program);
}


// Debug step by step
// -------------------------------------------------------------------------------------------------
// static
std::pair<code, size_t> interpreter::debug_start(program const& program) {
    code ec;

    if ( ! program.is_valid()) {
        return {error::invalid_script, 0};
    }

    // BCHN code:
    // bool const chipVmLimitsEnabled = (flags & SCRIPT_ENABLE_MAY2025) != 0;
    // size_t const maxScriptElementSize = chipVmLimitsEnabled ? may2025::MAX_SCRIPT_ELEMENT_SIZE
    //                                                         : MAX_SCRIPT_ELEMENT_SIZE_LEGACY;

    // if (chipVmLimitsEnabled && !metrics.HasValidScriptLimits() && context) {
    //     // Calculate metrics "scriptLimits", if not already passed-in, and if we have a `context` object
    //     // from which to get the scriptSig size.
    //     metrics.SetScriptLimits(flags, context->scriptSig().size());
    // }

    // TODO: flags vs forks
    // if (program.is_chip_vm_limits_enabled() && !program.get_metrics().has_valid_script_limits() && context) {
    //     // Calculate metrics "scriptLimits", if not already passed-in, and if we have a `context` object
    //     // from which to get the scriptSig size.
    //     program.get_metrics().set_script_limits(program.get_flags(), context->scriptSig().size());
    // }

    return {error::success, 0};
}

// static
bool interpreter::debug_steps_available(program const& program, size_t step) {
    // std::println("interpreter::debug_steps_available() step: {}", step);
    // std::println("interpreter::debug_steps_available() program.operation_count(): {}", program.operation_count());
    // std::println("interpreter::debug_steps_available() program.get_script().operations().size(): {}", program.get_script().operations().size());
    // return step <= program.operation_count();
    return step < program.get_script().operations().size();
}

// static
std::tuple<code, size_t, program> interpreter::debug_step(program program, size_t step) {
    // std::println("src/domain/src/machine/interpreter.cpp", "----------------------------------------");
    // std::println("interpreter::debug_step() BEGIN step: {}", step);
    // std::println("interpreter::debug_step() BEGIN program.get_script().operations().size(): {}", program.get_script().operations().size());

    // if (step > program.operation_count()) {
    if (step >= program.get_script().operations().size()) {
        return {error::invalid_operation_count, step, program};
    }

    auto const op_it = program.begin() + step;
    if (op_it == program.end()) {
        return {error::invalid_operation_count, step, program};
    }

    auto const op = *op_it;

    if (op.is_oversized(program.max_script_element_size())) {
        return {error::invalid_push_data_size, step, program};
    }

    if (op.is_disabled(program.forks())) {
        // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 4");
        return {error::op_disabled, step, program};
    }

    if ( ! program.increment_operation_count(op)) {
        // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 5");
        return {error::invalid_operation_count, step, program};
    }

    if (program.if_(op)) {
        code ec = run_op(op, program);
        if (ec != error::success) {
            // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 6");
            return {ec, step, program};
        }

        if (program.is_stack_overflow()) {
            // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 7");
            return {error::invalid_stack_size, step, program};
        }

        // Enforce May 2025 VM limits
        if (program.is_chip_vm_limits_enabled()) {
            // // Check that this opcode did not cause us to exceed opCost and/or hashIters limits.
            // // Note: `metrics` may lack a valid "scriptLimits" object in rare cases (tests only), in which case
            // // the below two limit checks are always going to return false.

            // if (metrics.IsOverOpCostLimit(flags)) {
            //     return set_error(serror, ScriptError::OP_COST);
            // }
            // if (metrics.IsOverHashItersLimit()) {
            //     return set_error(serror, ScriptError::TOO_MANY_HASH_ITERS);
            // }

            // // Conditional stack may not exceed depth of 100
            // if (vfExec.size() > may2025::MAX_CONDITIONAL_STACK_DEPTH) {
            //     return set_error(serror, ScriptError::CONDITIONAL_STACK_DEPTH);
            // }


            // TODO: flags vs forks
            // if (program.get_metrics().is_over_op_cost_limit(program.get_flags())) {
            //     return error::op_cost;
            // }

            if (program.get_metrics().is_over_hash_iters_limit()) {
                // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 8");
                return {error::too_many_hash_iters, step, program};
            }

            // Conditional stack may not exceed depth of 100
            if (program.conditional_stack_size() > ::kth::may2025::max_conditional_stack_depth) {
                // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 9");
                return {error::conditional_stack_depth, step, program};
            }
        }
    }

    // std::println("interpreter::debug_step() END step: {}", ++step);
    // std::println("interpreter::debug_step() END program.get_script().operations().size(): {}", program.get_script().operations().size());
    // std::println("src/domain/src/machine/interpreter.cpp", "----------------------------------------");

    return {error::success, ++step, program};
}

// static
code interpreter::debug_end(program const& program) {
    return program.closed() ? error::success : error::invalid_stack_scope;
}

} // namespace kth::domain::machine
