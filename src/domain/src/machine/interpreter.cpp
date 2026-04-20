// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/interpreter.hpp>

#include <kth/domain/constants.hpp>

namespace kth::domain::machine {

namespace {

using function_table_t = boost::unordered_flat_map<data_chunk, data_chunk>;

// Forward-declared: `handle_op_invoke` recurses into `run_script`
// for the callee's body.
op_result run_script(program& prog, operation::list const& ops,
                     function_table_t& function_table,
                     size_t& invoke_depth, size_t outer_loop_depth = 0);

// ── Shared per-op handlers ─────────────────────────────────────────
// Extracted so `run_script` and `step_one` don't reimplement the same
// opcode logic in two shapes. Each helper takes references to the
// caller's state containers — both `run_script`'s locals and
// `step_one`'s `debug_snapshot` members fit the same signatures, so
// no adapters are needed.
//
// `OP_BEGIN` / `OP_UNTIL` stay inline in each caller because they
// touch the iterator/index model specific to that caller (iterator
// rewind in `run_script`; index rewind in `step_one`); factoring them
// out would require templating on the jump-target type for five lines
// of body each, which is net noise.

// Pushes `next_pos` onto `loop_stack` so a matching OP_UNTIL can
// rewind here. `next_pos` is the index of the op immediately after
// OP_BEGIN (i.e. the first op of the loop body).
op_result handle_op_begin(program& prog, operation const& op,
                          std::vector<size_t>& loop_stack,
                          std::vector<bool>& control_stack,
                          size_t next_pos) {
    if ( ! chain::script::is_enabled(prog.flags(), script_flags::bch_loops)) {
        return {error::op_reserved, op.code()};
    }
    loop_stack.push_back(next_pos);
    control_stack.push_back(true);
    return {};
}

// When the top-of-stack truthy check fails, rewrites `next_pos_inout`
// to the loop head to iterate again; otherwise closes the frame.
op_result handle_op_until(program& prog, operation const& op,
                          std::vector<size_t>& loop_stack,
                          std::vector<bool>& control_stack,
                          size_t& next_pos_inout) {
    if ( ! chain::script::is_enabled(prog.flags(), script_flags::bch_loops)) {
        return {error::op_reserved, op.code()};
    }
    if (control_stack.empty() || ! control_stack.back()) {
        return {error::invalid_stack_scope, op.code()};
    }
    bool condition = true;
    if (prog.succeeded()) {
        if (prog.empty()) {
            return {error::insufficient_main_stack, op.code()};
        }
        condition = prog.stack_true(false);
        prog.drop();
    }
    if ( ! condition) {
        next_pos_inout = loop_stack.back();  // rewind to the loop head
    } else {
        loop_stack.pop_back();
        control_stack.pop_back();
    }
    return {};
}

// Per-op preflight shared by run_script and step_one.
op_result preflight_op(program& prog, operation const& op) {
    if (op.is_oversized(prog.max_script_element_size())) {
        return {error::invalid_push_data_size, op.code()};
    }
    if (op.is_disabled(prog.flags())) {
        return {op.disabled_error(), op.code()};
    }
    if ( ! prog.increment_operation_count(op)) {
        return {error::invalid_operation_count, op.code()};
    }
    return {};
}

// OP_DEFINE: pops <body><id>, inserts into function_table.
op_result handle_op_define(program& prog, operation const& op,
                           function_table_t& function_table) {
    if ( ! chain::script::is_enabled(prog.flags(), script_flags::bch_subroutines)) {
        return {error::op_reserved, op.code()};
    }
    if (prog.size() < 2) {
        return {error::insufficient_main_stack, op.code()};
    }
    auto func_id = prog.pop();
    if (func_id.size() > 7) {
        return {error::invalid_operand_size, op.code()};
    }
    auto func_code = prog.pop();
    if (func_code.size() > max_script_size) {
        return {error::invalid_script, op.code()};
    }
    auto const code_size = func_code.size();
    auto const [_, inserted] = function_table.try_emplace(
        std::move(func_id), std::move(func_code));
    if ( ! inserted) {
        return {error::invalid_script, op.code()};  // overwrite disallowed
    }
    prog.get_metrics().add_op_cost(code_size);
    return {};
}

// OP_INVOKE: pops <id>, pushes a frame, recurses into `run_script`
// for the callee's body. `total_outer_loop_depth` is the caller's
// accumulated outer depth plus its own `loop_stack.size()` (i.e. what
// the callee should see as ITS outer depth).
op_result handle_op_invoke(program& prog, operation const& op,
                           function_table_t& function_table,
                           size_t& invoke_depth,
                           size_t total_outer_loop_depth) {
    if ( ! chain::script::is_enabled(prog.flags(), script_flags::bch_subroutines)) {
        return {error::op_reserved, op.code()};
    }
    if (prog.empty()) {
        return {error::insufficient_main_stack, op.code()};
    }
    auto func_id = prog.pop();
    if (func_id.size() > 7) {
        return {error::invalid_operand_size, op.code()};
    }
    auto const fit = function_table.find(func_id);
    if (fit == function_table.end()) {
        return {error::invalid_script, op.code()};  // undefined function
    }
    chain::script func_script(fit->second, false);
    auto const condition_depth_before = prog.conditional_stack_size();
    ++invoke_depth;
    if (prog.is_chip_vm_limits_enabled()) {
        // Pre-entry depth check — must stay in sync with the per-op
        // check in `check_post_op_vm_limits`. `conditional_stack_size`
        // covers IFs across all frames (program-level); the outer loop
        // count comes from the caller because `loop_stack` is
        // per-frame.
        auto const total_depth = prog.conditional_stack_size()
            + total_outer_loop_depth + invoke_depth;
        if (total_depth > ::kth::may2025::max_conditional_stack_depth) {
            --invoke_depth;
            return {error::conditional_stack_depth, op.code()};
        }
    }
    prog.set_active_script(&func_script);
    auto const inner = run_script(prog, func_script.operations(),
                                  function_table, invoke_depth,
                                  total_outer_loop_depth);
    --invoke_depth;
    prog.reset_active_script();
    if ( ! inner) {
        // Preserve the inner opcode attribution — the subroutine body
        // owns the failure site, not the invoking OP_INVOKE.
        return inner;
    }
    if (prog.conditional_stack_size() != condition_depth_before) {
        return {error::invalid_stack_scope, op.code()};
    }
    return {};
}

// OP_CODESEPARATOR anchors the active-bytecode-hash marker at the
// op right after `current_idx`. Special-cased out of the uniform
// `interpreter::run(op, prog)` dispatch because the marker needs the
// op's script position — `run_op` tallies the base 100 op_cost for
// every executable op it dispatches, so bypassing it means we must
// tally the same cost manually here.
op_result handle_op_codeseparator(program& prog, size_t current_idx) {
    prog.get_metrics().add_op_cost(::kth::may2025::opcode_cost);
    if ( ! prog.mark_code_separator(current_idx)) {
        return {error::invalid_script, opcode::codeseparator};
    }
    return {};
}

// Non-special-form dispatch: minimaldata check, IF/ELSE/ENDIF
// structural tracking, actual op dispatch via `interpreter::run(op)`,
// and the post-op stack overflow check.
op_result handle_conditional_dispatch(program& prog, operation const& op,
                                      std::vector<bool>& control_stack,
                                      size_t function_table_size) {
    if (op.code() <= opcode::push_four_size
        && chain::script::is_enabled(prog.flags(), script_flags::bch_minimaldata)
        && ! op.is_minimal_push()) {
        return {error::minimaldata, op.code()};
    }

    if (chain::script::is_enabled(prog.flags(), script_flags::bch_loops)) {
        if (op.code() == opcode::if_ || op.code() == opcode::notif) {
            control_stack.push_back(false);  // false = IF block
        } else if (op.code() == opcode::else_) {
            if ( ! control_stack.empty() && control_stack.back()) {
                return {error::invalid_stack_scope, op.code()};
            }
        } else if (op.code() == opcode::endif) {
            if ( ! control_stack.empty() && control_stack.back()) {
                return {error::invalid_stack_scope, op.code()};
            }
            if ( ! control_stack.empty()) {
                control_stack.pop_back();
            }
        }
    }

    if (auto const res = interpreter::run(op, prog); ! res) {
        return res;
    }
    if (prog.is_stack_overflow(function_table_size)) {
        return {error::invalid_stack_size, op.code()};
    }
    return {};
}

// Post-op VM-limits check (May 2025 CHIP): op_cost, hash iters, and
// conditional/loop stack depth. `total_depth` is the caller's full
// accumulated depth — `conditional_stack_size + outer_loop_depth +
// local loop_stack.size() + invoke_depth`.
op_result check_post_op_vm_limits(program& prog, operation const& op,
                                  size_t total_depth) {
    if ( ! prog.is_chip_vm_limits_enabled()) {
        return {};
    }
    if (prog.get_metrics().is_over_op_cost_limit()) {
        return {error::op_cost_limit, op.code()};
    }
    if (prog.get_metrics().is_over_hash_iters_limit()) {
        return {error::too_many_hash_iters, op.code()};
    }
    if (total_depth > ::kth::may2025::max_conditional_stack_depth) {
        return {error::conditional_stack_depth, op.code()};
    }
    return {};
}

// Shared per-script driver. Both `interpreter::run()` and the
// OP_INVOKE handler inside `step_one` go through this helper — so
// every per-op rule (disabled-op / op_count / loop / subroutine /
// minimal-push / stack overflow / VM limits) has exactly one
// implementation. `function_table` and `invoke_depth` are threaded
// by reference so OP_DEFINE scopes and the VM depth limit straddle
// nested OP_INVOKE calls correctly.
//
// `outer_loop_depth` is the sum of `loop_stack.size()` from every
// enclosing frame (0 at the outermost call). `conditional_stack_size()`
// on `prog` already includes IFs from all frames because `condition_`
// is a program-level member, but `loop_stack` is a per-frame local, so
// without threading its outer total the VM-limits depth check inside a
// callee would miss caller-side OP_BEGINs and let a script exceed
// `max_conditional_stack_depth` (consensus — matches BCHN's
// `EvalStack::depth()` which sums cumulativeCtr across frames).
op_result run_script(
    program& prog,
    operation::list const& ops,
    function_table_t& function_table,
    size_t& invoke_depth,
    size_t outer_loop_depth
) {
    std::vector<size_t> loop_stack;
    // Unified control stack: true = BEGIN (loop), false = IF.
    // Ensures ENDIF/ELSE only close IF blocks, not loops.
    std::vector<bool> control_stack;

    // Index-based iteration (same shape as `step_one`). Cheaper than
    // nothing at runtime (`ops[i]` is equivalent to `*it` for
    // `std::vector`) and lets the shared `handle_op_begin`/`_until`
    // helpers take a single `size_t` position instead of having to be
    // templated on iterator vs. index.
    size_t idx = 0;
    auto const sz = ops.size();

    while (idx < sz) {
        size_t const current_idx = idx;
        auto const& op = ops[idx];
        ++idx;

        if (auto const pre = preflight_op(prog, op); ! pre) {
            return pre;
        }

        if (op.code() == opcode::op_begin) {
            if (auto const res = handle_op_begin(prog, op, loop_stack,
                                                 control_stack, idx);
                ! res) return res;
        } else if (op.code() == opcode::op_until) {
            if (auto const res = handle_op_until(prog, op, loop_stack,
                                                 control_stack, idx);
                ! res) return res;
        } else if (op.code() == opcode::op_define && prog.succeeded()) {
            if (auto const res = handle_op_define(prog, op, function_table); ! res) {
                return res;
            }
        } else if (op.code() == opcode::op_invoke && prog.succeeded()) {
            if (auto const res = handle_op_invoke(prog, op, function_table,
                                                  invoke_depth,
                                                  outer_loop_depth + loop_stack.size());
                ! res) {
                return res;
            }
        } else if (op.code() == opcode::codeseparator && prog.if_(op)) {
            if (auto const res = handle_op_codeseparator(prog, current_idx); ! res) {
                return res;
            }
        } else if (prog.if_(op)) {
            if (auto const res = handle_conditional_dispatch(prog, op,
                                                             control_stack,
                                                             function_table.size());
                ! res) {
                return res;
            }
        } else {
            prog.get_metrics().add_op_cost(::kth::may2025::opcode_cost);
        }

        auto const total_depth = prog.conditional_stack_size()
            + outer_loop_depth + loop_stack.size() + invoke_depth;
        if (auto const res = check_post_op_vm_limits(prog, op, total_depth); ! res) {
            return res;
        }
    }

    // Structural / post-loop errors are not attributable to a specific
    // opcode, so leave `op_result::op` empty on these exits.
    if ( ! loop_stack.empty()) {
        return {error::invalid_stack_scope, std::nullopt};
    }
    return {};
}

// Common pre-run setup: script-size bound and May 2025 VM-limits
// initialization from the transaction context. Shared by
// `interpreter::run()` and `debug_begin`.
op_result pre_run_setup(program& prog) {
    if ( ! prog.is_valid()) {
        return {error::invalid_script, std::nullopt};
    }
    if (prog.get_script().serialized_size(false) > max_script_size) {
        return {error::invalid_script, std::nullopt};
    }
    if (prog.is_chip_vm_limits_enabled()
        && ! prog.get_metrics().has_valid_script_limits()) {
        auto const& ctx = prog.context();
        if (ctx) {
            auto const& tx = ctx->transaction();
            auto const input_idx = ctx->input_index();
            if (input_idx < tx.inputs().size()) {
                auto const script_sig_size = tx.inputs()[input_idx]
                    .script().serialized_size(false);
                prog.get_metrics().set_native_script_limits(false, script_sig_size);
            }
        }
    }
    return {};
}

} // namespace

op_result interpreter::run(program& program) {
    if (auto const setup = pre_run_setup(program); ! setup) {
        return setup;
    }

    // Function table for OP_DEFINE/OP_INVOKE (May 2026). Shared
    // across every frame of this run via reference.
    function_table_t function_table;
    size_t invoke_depth = 0;

    auto const outcome = run_script(program, program.get_script().operations(),
                                    function_table, invoke_depth);
    if ( ! outcome) return outcome;

    return program.closed() ? op_result{} : op_result{error::invalid_stack_scope, std::nullopt};
}

op_result interpreter::run(operation const& op, program& program) {
    return run_op(op, program);
}


// Debug step by step
// -------------------------------------------------------------------------------------------------
//
// `debug_step` is the atom; every other debug entry point builds on
// top of it. Each call copies the snapshot by value — forward replay
// is deterministic, so holding a history of snapshots lets the caller
// rewind to any prior step. True reverse execution is not generally
// possible (hash / EC / arithmetic-with-overflow ops are not
// invertible), so snapshot history is the only way to go "back".

namespace {

// One-step advance on an otherwise-ready snapshot. Mirrors the body
// of `run_script` above using the snapshot's `loop_stack` /
// `control_stack` / `function_table` / `invoke_depth` for the
// cross-step state. OP_INVOKE delegates to `run_script` on the
// callee's ops — that's "step-over": the whole function body runs
// atomically within a single `debug_step`. Step-INTO (pushing a call
// frame so the caller can iterate through the function body
// one op at a time) is future work tied to BCH 2026-May surfacing.
op_result step_one(debug_snapshot& s) {
    auto const& ops = s.prog.get_script().operations();
    if (s.step >= ops.size()) {
        return {error::invalid_operation_count, std::nullopt};
    }

    auto const& op = ops[s.step];
    size_t const current_idx = s.step;
    // Normal progression; OP_UNTIL may override with a backward jump.
    size_t next_step = s.step + 1;

    if (auto const pre = preflight_op(s.prog, op); ! pre) {
        return pre;
    }

    if (op.code() == opcode::op_begin) {
        if (auto const res = handle_op_begin(s.prog, op, s.loop_stack,
                                             s.control_stack, next_step);
            ! res) return res;
    } else if (op.code() == opcode::op_until) {
        if (auto const res = handle_op_until(s.prog, op, s.loop_stack,
                                             s.control_stack, next_step);
            ! res) return res;
    } else if (op.code() == opcode::op_define && s.prog.succeeded()) {
        if (auto const res = handle_op_define(s.prog, op, s.function_table); ! res) {
            return res;
        }
    } else if (op.code() == opcode::op_invoke && s.prog.succeeded()) {
        if (auto const res = handle_op_invoke(s.prog, op, s.function_table,
                                              s.invoke_depth,
                                              s.outer_loop_depth + s.loop_stack.size());
            ! res) {
            return res;
        }
    } else if (op.code() == opcode::codeseparator && s.prog.if_(op)) {
        if (auto const res = handle_op_codeseparator(s.prog, current_idx); ! res) {
            return res;
        }
    } else if (s.prog.if_(op)) {
        if (auto const res = handle_conditional_dispatch(s.prog, op,
                                                         s.control_stack,
                                                         s.function_table.size());
            ! res) {
            return res;
        }
    } else {
        s.prog.get_metrics().add_op_cost(::kth::may2025::opcode_cost);
    }

    auto const total_depth = s.prog.conditional_stack_size()
        + s.outer_loop_depth + s.loop_stack.size() + s.invoke_depth;
    if (auto const res = check_post_op_vm_limits(s.prog, op, total_depth); ! res) {
        return res;
    }

    s.step = next_step;
    return {};
}

} // namespace

// static
debug_snapshot interpreter::debug_begin(program prog) {
    debug_snapshot s{std::move(prog)};
    if (auto const setup = pre_run_setup(s.prog); ! setup) {
        s.last = setup;
        s.done = true;
        return s;
    }
    // An empty script has nothing to step through; mark the snapshot
    // done up front so `debug_run` / `debug_step` don't synthesise a
    // spurious `invalid_operation_count` on the first tick.
    if (s.prog.get_script().operations().empty()) {
        s.done = true;
    }
    return s;
}

// static
debug_snapshot interpreter::debug_step(debug_snapshot snapshot) {
    if (snapshot.done) return snapshot;

    snapshot.last = step_one(snapshot);
    if ( ! snapshot.last) {
        snapshot.done = true;
        return snapshot;
    }
    if (snapshot.step >= snapshot.prog.get_script().operations().size()) {
        snapshot.done = true;
    }
    return snapshot;
}

// static
debug_snapshot interpreter::debug_step_n(debug_snapshot snapshot, size_t n) {
    for (size_t i = 0; i < n && ! snapshot.done; ++i) {
        snapshot = debug_step(std::move(snapshot));
    }
    return snapshot;
}

// static
debug_snapshot interpreter::debug_step_until(
    debug_snapshot snapshot,
    std::function<bool(debug_snapshot const&)> const& predicate) {
    // Test the predicate BEFORE stepping so a breakpoint set on the
    // current state (or on the initial snapshot from `debug_begin`)
    // fires without advancing — otherwise every call would execute at
    // least one op and such breakpoints would be unreachable.
    while ( ! snapshot.done) {
        if (predicate(snapshot)) break;
        snapshot = debug_step(std::move(snapshot));
    }
    return snapshot;
}

// static
debug_snapshot interpreter::debug_run(debug_snapshot snapshot) {
    while ( ! snapshot.done) {
        snapshot = debug_step(std::move(snapshot));
    }
    return snapshot;
}

// static
std::vector<debug_snapshot> interpreter::debug_run_traced(debug_snapshot start) {
    std::vector<debug_snapshot> trace;
    trace.push_back(std::move(start));
    while ( ! trace.back().done) {
        // Capture the next snapshot into a local first so we don't
        // pass a reference to `trace.back()` through a call whose
        // return value then gets moved into the vector — the pattern
        // would be C++17-only and fragile against a refactor that
        // had `debug_step` taking its argument by reference.
        auto next = debug_step(trace.back());
        trace.push_back(std::move(next));
    }
    return trace;
}

// static
op_result interpreter::debug_finalize(debug_snapshot const& snapshot) {
    if ( ! snapshot.last) return snapshot.last;
    if ( ! snapshot.done) return {error::invalid_operation_count, std::nullopt};
    // Unterminated IF/ELSE or loop at end-of-script is the same
    // structural error `run()` surfaces.
    if ( ! snapshot.control_stack.empty()) return {error::invalid_stack_scope, std::nullopt};
    if ( ! snapshot.loop_stack.empty()) return {error::invalid_stack_scope, std::nullopt};
    return snapshot.prog.closed() ? op_result{} : op_result{error::invalid_stack_scope, std::nullopt};
}

} // namespace kth::domain::machine
