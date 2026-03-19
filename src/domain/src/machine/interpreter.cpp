// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/interpreter.hpp>

#include <kth/domain/constants.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace kth::domain::machine {

code interpreter::run(program& program) {
    if ( ! program.is_valid()) {
        return error::invalid_script;
    }

    auto const script_sz = program.get_script().serialized_size(false);
    if (script_sz > max_script_size) {
        return error::invalid_script;
    }

    // Initialize VM limits if May 2025 is enabled and limits are not yet set.
    // The op_cost and hash_iters limits depend on the scriptSig size.
    if (program.is_chip_vm_limits_enabled() && ! program.get_metrics().has_valid_script_limits()) {
        auto const& ctx = program.context();
        if (ctx) {
            auto const& tx = ctx->transaction();
            auto const input_idx = ctx->input_index();
            if (input_idx < tx.inputs().size()) {
                auto const script_sig_size = tx.inputs()[input_idx].script().serialized_size(false);
                program.get_metrics().set_native_script_limits(false, script_sig_size);
            }
        }
    }

    // Function table for OP_DEFINE/OP_INVOKE (May 2026). Shared across all frames.
    boost::unordered_flat_map<data_chunk, data_chunk> function_table;
    size_t invoke_depth = 0;

    // Recursive lambda to execute a script (main or invoked function).
    auto const run_script = [&](auto const& self, operation::list const& ops) -> code {
        using op_iter = operation::list::const_iterator;
        std::vector<op_iter> loop_stack;
        // Unified control stack: true = BEGIN (loop), false = IF.
        // Used to verify that ENDIF/ELSE only close IF blocks, not loops.
        std::vector<bool> control_stack;

        auto it = ops.begin();
        auto const end = ops.end();

        while (it != end) {
            auto const& op = *it;
            ++it;

            if (op.is_oversized(program.max_script_element_size())) {
                return error::invalid_push_data_size;
            }

            if (op.is_disabled(program.flags())) {
                return op.disabled_error();
            }

            if ( ! program.increment_operation_count(op)) {
                return error::invalid_operation_count;
            }

            // OP_BEGIN/OP_UNTIL: processed unconditionally for structure tracking.
            if (op.code() == opcode::op_begin) {
                if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_loops)) {
                    return error::op_reserved;
                }
                loop_stack.push_back(it);
                control_stack.push_back(true);  // true = loop
            } else if (op.code() == opcode::op_until) {
                if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_loops)) {
                    return error::op_reserved;
                }
                if (control_stack.empty() || ! control_stack.back()) {
                    return error::invalid_stack_scope;  // innermost is IF, not BEGIN
                }
                bool condition = true;
                if (program.succeeded()) {
                    if (program.empty()) {
                        return error::insufficient_main_stack;
                    }
                    condition = program.stack_true(false);
                    program.drop();
                }
                if ( ! condition) {
                    it = loop_stack.back();
                } else {
                    loop_stack.pop_back();
                    control_stack.pop_back();
                }
            // OP_DEFINE: store function in shared table.
            } else if (op.code() == opcode::op_define && program.succeeded()) {
                if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_subroutines)) {
                    return error::op_reserved;
                }
                if (program.size() < 2) {
                    return error::insufficient_main_stack;
                }
                auto func_id = program.pop();
                if (func_id.size() > 7) {
                    return error::invalid_operand_size;
                }
                auto func_code = program.pop();
                if (func_code.size() > max_script_size) {
                    return error::invalid_script;
                }
                auto const code_size = func_code.size();
                auto const [_, inserted] = function_table.try_emplace(std::move(func_id), std::move(func_code));
                if ( ! inserted) {
                    return error::invalid_script;  // overwrite disallowed
                }
                program.get_metrics().add_op_cost(code_size);
            // OP_INVOKE: recursively execute function bytecode.
            } else if (op.code() == opcode::op_invoke && program.succeeded()) {
                if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_subroutines)) {
                    return error::op_reserved;
                }
                if (program.empty()) {
                    return error::insufficient_main_stack;
                }
                auto func_id = program.pop();
                if (func_id.size() > 7) {
                    return error::invalid_operand_size;
                }
                auto const fit = function_table.find(func_id);
                if (fit == function_table.end()) {
                    return error::invalid_script;  // undefined function
                }
                chain::script func_script(fit->second, false);
                auto const condition_depth_before = program.conditional_stack_size();
                ++invoke_depth;
                // Check depth limit BEFORE entering (each INVOKE adds 1 to depth).
                if (program.is_chip_vm_limits_enabled()) {
                    auto const total_depth = program.conditional_stack_size() + control_stack.size() + invoke_depth;
                    if (total_depth > ::kth::may2025::max_conditional_stack_depth) {
                        --invoke_depth;
                        return error::conditional_stack_depth;
                    }
                }
                program.set_active_script(&func_script);
                auto const ec = self(self, func_script.operations());
                --invoke_depth;
                program.reset_active_script();
                if (ec != error::success) {
                    return ec;
                }
                // Verify function didn't leave unbalanced IF/ELSE/ENDIF.
                if (program.conditional_stack_size() != condition_depth_before) {
                    return error::invalid_stack_scope;
                }
            } else if (program.if_(op)) {
                if (op.code() <= opcode::push_four_size
                    && chain::script::is_enabled(program.flags(), script_flags::bch_minimaldata)
                    && ! op.is_minimal_push()) {
                    return error::minimaldata;
                }

                // Track IF/ELSE/ENDIF in the unified control stack (May 2026).
                if (chain::script::is_enabled(program.flags(), script_flags::bch_loops)) {
                    if (op.code() == opcode::if_ || op.code() == opcode::notif) {
                        control_stack.push_back(false);  // false = IF block
                    } else if (op.code() == opcode::else_) {
                        if ( ! control_stack.empty() && control_stack.back()) {
                            return error::invalid_stack_scope;  // innermost is BEGIN, not IF
                        }
                    } else if (op.code() == opcode::endif) {
                        if ( ! control_stack.empty() && control_stack.back()) {
                            return error::invalid_stack_scope;  // innermost is BEGIN, not IF
                        }
                        if ( ! control_stack.empty()) {
                            control_stack.pop_back();
                        }
                    }
                }

                if (auto const res = run_op(op, program); res) {
                    return res.error;
                }
                if (program.is_stack_overflow(function_table.size())) {
                    return error::invalid_stack_size;
                }
            } else {
                program.get_metrics().add_op_cost(::kth::may2025::opcode_cost);
            }

            // VM limits
            if (program.is_chip_vm_limits_enabled()) {
                if (program.get_metrics().is_over_op_cost_limit()) {
                    return error::op_cost_limit;
                }
                if (program.get_metrics().is_over_hash_iters_limit()) {
                    return error::too_many_hash_iters;
                }
                auto const total_depth = program.conditional_stack_size() + loop_stack.size() + invoke_depth;
                if (total_depth > ::kth::may2025::max_conditional_stack_depth) {
                    return error::conditional_stack_depth;
                }
            }
        }

        if ( ! loop_stack.empty()) {
            return error::invalid_stack_scope;
        }
        return error::success;
    };

    auto const ec = run_script(run_script, program.get_script().operations());
    if (ec != error::success) return ec;

    return program.closed() ? error::success : error::invalid_stack_scope;
}

code interpreter::run(operation const& op, program& program) {
    auto const res = run_op(op, program);
    return res.error;
}


// Debug step by step
// -------------------------------------------------------------------------------------------------
// static
std::pair<code, size_t> interpreter::debug_start(program const& program) {
    if ( ! program.is_valid()) {
        return {error::invalid_script, 0};
    }

    // Note: VM limits initialization is done in run() and debug_step() which take non-const program.
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

    if (op.is_disabled(program.flags())) {
        // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 4");
        return {op.disabled_error(), step, program};
    }

    if ( ! program.increment_operation_count(op)) {
        // std::println("src/domain/src/machine/interpreter.cpp", "interpreter::debug_step() return 5");
        return {error::invalid_operation_count, step, program};
    }

    if (program.if_(op)) {
        auto const res = run_op(op, program);
        if (res) {
            return {res.error, step, program};
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


            // TODO: flags
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
