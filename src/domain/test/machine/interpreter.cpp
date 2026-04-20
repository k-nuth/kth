// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/error.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::machine;

namespace {

// `program` stores pointers into the script and transaction it was
// constructed from, so both MUST outlive the program (and any debug
// snapshot holding the program by value). Tests keep both as locals
// in the TEST_CASE scope; `script_of` / `dummy_tx` just save typing.
chain::script script_of(std::initializer_list<operation> ops) {
    return chain::script(operation::list{ops.begin(), ops.end()});
}

chain::transaction const& dummy_tx() {
    static chain::transaction const tx;
    return tx;
}

} // namespace

// ---------------------------------------------------------------------------
// op_result
// ---------------------------------------------------------------------------

TEST_CASE("op_result default is success with no opcode", "[op_result]") {
    op_result r;
    REQUIRE(bool(r));                              // operator bool = success
    REQUIRE(r.error == error::success);
    REQUIRE_FALSE(r.op.has_value());
}

TEST_CASE("op_result from error code is failure with no opcode", "[op_result]") {
    op_result r{error::invalid_script, std::nullopt};
    REQUIRE_FALSE(bool(r));
    REQUIRE(r.error == error::invalid_script);
    REQUIRE_FALSE(r.op.has_value());
}

TEST_CASE("op_result with opcode carries both fields", "[op_result]") {
    op_result r{error::incorrect_signature, opcode::checksig};
    REQUIRE_FALSE(bool(r));
    REQUIRE(r.error == error::incorrect_signature);
    REQUIRE(r.op.has_value());
    REQUIRE(*r.op == opcode::checksig);
}

TEST_CASE("op_result converts to code on direct-init", "[op_result]") {
    op_result r{error::invalid_script, std::nullopt};
    // Conversion is `explicit` (see op_result::operator code()), so
    // copy-init (`code ec = r;`) is intentionally rejected — direct-
    // init is the audited bridge to the legacy `kth::code` API.
    code ec{r};
    REQUIRE(ec == error::invalid_script);
}

// ---------------------------------------------------------------------------
// debug_begin / debug_step
// ---------------------------------------------------------------------------

TEST_CASE("debug_step advances through the script", "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto snap = interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0));
    REQUIRE_FALSE(snap.done);
    REQUIRE(snap.step == 0);

    snap = interpreter::debug_step(std::move(snap));
    REQUIRE(bool(snap.last));
    REQUIRE(snap.step == 1);
    REQUIRE_FALSE(snap.done);

    snap = interpreter::debug_step(std::move(snap));
    REQUIRE(bool(snap.last));
    REQUIRE(snap.step == 2);
    REQUIRE(snap.done);                         // past the last op
}

TEST_CASE("debug_step on a done snapshot is a no-op", "[interpreter][debug]") {
    auto scr = script_of({operation(opcode::push_size_0)});
    auto snap = interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0));
    snap = interpreter::debug_step(std::move(snap));
    REQUIRE(snap.done);
    auto const before_step = snap.step;

    snap = interpreter::debug_step(std::move(snap));
    REQUIRE(snap.done);
    REQUIRE(snap.step == before_step);          // did not advance
}

// ---------------------------------------------------------------------------
// Rewind semantics — snapshot-per-step
// ---------------------------------------------------------------------------

TEST_CASE("debug_step preserves the input snapshot (rewind-friendly)",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto start = interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0));
    auto advanced = interpreter::debug_step(start);

    REQUIRE(start.step == 0);                   // start is unchanged
    REQUIRE(advanced.step == 1);                // new snapshot moved forward
}

// ---------------------------------------------------------------------------
// Batch helpers: step_n / run / run_traced / step_until
// ---------------------------------------------------------------------------

TEST_CASE("debug_step_n advances by N (or stops at done)",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto snap = interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0));
    snap = interpreter::debug_step_n(std::move(snap), 2);
    REQUIRE(snap.step == 2);
    REQUIRE_FALSE(snap.done);

    // Asking for more steps than remain stops at the end.
    snap = interpreter::debug_step_n(std::move(snap), 10);
    REQUIRE(snap.done);
}

TEST_CASE("debug_run consumes the whole script", "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto snap = interpreter::debug_run(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE(snap.done);
    REQUIRE(bool(snap.last));
}

TEST_CASE("debug_run_traced yields one snapshot per step",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto trace = interpreter::debug_run_traced(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    // Initial + 2 steps = 3 entries.
    REQUIRE(trace.size() == 3);
    REQUIRE(trace.front().step == 0);
    REQUIRE_FALSE(trace.front().done);
    REQUIRE(trace.back().done);
}

TEST_CASE("debug_step_until stops when the predicate fires",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    auto snap = interpreter::debug_step_until(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)),
        [](debug_snapshot const& s) { return s.step == 2; });
    REQUIRE(snap.step == 2);
    REQUIRE_FALSE(snap.done);                   // predicate stopped us early
}

// ---------------------------------------------------------------------------
// interpreter::run (full-script)
// ---------------------------------------------------------------------------

TEST_CASE("run on a well-formed script returns success", "[interpreter][run]") {
    auto scr = script_of({operation(opcode::push_positive_1)});
    program prog(scr, dummy_tx(), 0, 0, 0);
    auto const result = interpreter::run(prog);
    REQUIRE(bool(result));
    REQUIRE(result.error == error::success);
}

TEST_CASE("run on a script that fails carries the offending opcode",
          "[interpreter][run]") {
    // `op_verify` on an empty stack fails — the interpreter surfaces
    // the opcode that raised the error.
    auto scr = script_of({operation(opcode::verify)});
    program prog(scr, dummy_tx(), 0, 0, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.error != error::success);
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::verify);
}

TEST_CASE("run bridges to code for legacy callers via direct-init",
          "[interpreter][run]") {
    auto scr = script_of({operation(opcode::push_positive_1)});
    program prog(scr, dummy_tx(), 0, 0, 0);
    code ec{interpreter::run(prog)};
    REQUIRE(ec == error::success);
}

// ---------------------------------------------------------------------------
// interpreter::run(op, program) — single-op driver
// ---------------------------------------------------------------------------

TEST_CASE("run(op, program) executes a single opcode", "[interpreter][run]") {
    program prog;
    auto const result = interpreter::run(operation(opcode::push_positive_1), prog);
    REQUIRE(bool(result));
    REQUIRE(prog.size() == 1);             // push left the value on the stack
}

TEST_CASE("run(op, program) reports the opcode on failure",
          "[interpreter][run]") {
    program prog;
    auto const result = interpreter::run(operation(opcode::verify), prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::verify);
}

TEST_CASE("run(op, program) rejects OP_CODESEPARATOR standalone",
          "[interpreter][run][codeseparator]") {
    // OP_CODESEPARATOR needs the op's index in the active script to
    // anchor the active-bytecode-hash marker. The single-op driver
    // doesn't have that context (it runs an op against an arbitrary
    // program, with no script position), so it must refuse rather
    // than fabricating a position. The full-script / debug paths
    // special-case codeseparator in their per-op loops and call
    // `program::mark_code_separator(current_idx)` directly.
    program prog;
    auto const result = interpreter::run(operation(opcode::codeseparator), prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.error == error::not_implemented);
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::codeseparator);
}

// ---------------------------------------------------------------------------
// Error propagation through debug_step
// ---------------------------------------------------------------------------

TEST_CASE("debug_step records the offending opcode on failure and marks done",
          "[interpreter][debug]") {
    // `verify` with an empty stack errors out; the snapshot's `last`
    // should carry both the error code and the `verify` opcode.
    auto scr = script_of({operation(opcode::verify)});
    auto snap = interpreter::debug_step(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE_FALSE(bool(snap.last));
    REQUIRE(snap.last.op.has_value());
    REQUIRE(*snap.last.op == opcode::verify);
    REQUIRE(snap.done);                         // no further stepping after error
}

TEST_CASE("debug_run stops on error and preserves the error snapshot",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_positive_1),
        operation(opcode::verify),              // pops the 1, succeeds
        operation(opcode::verify),              // empty stack → error
    });
    auto snap = interpreter::debug_run(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE(snap.done);
    REQUIRE_FALSE(bool(snap.last));
    REQUIRE(snap.last.op.has_value());
    REQUIRE(*snap.last.op == opcode::verify);
    // Failure happened on the third op — we ran the first two.
    REQUIRE(snap.step == 2);
}

// ---------------------------------------------------------------------------
// run / debug parity regressions
// ---------------------------------------------------------------------------

// `OP_CODESEPARATOR` updates the "active" jump register via the PC
// that the interpreter loop publishes before each op dispatch (see
// `program::mark_code_separator(pc)`). An earlier
// revision did an address-identity search over the script's operation
// list, which silently broke whenever a caller happened to pass a
// copy of the op instead of a reference (`step_one` had exactly that
// bug — `run()` succeeded but `debug_run()` reported
// `error::invalid_script, opcode::codeseparator`). This test pins
// both paths against regression across the whole mechanism.
TEST_CASE("OP_CODESEPARATOR succeeds through run() and debug_run() alike",
          "[interpreter][debug][parity]") {
    auto scr = script_of({
        operation(opcode::push_positive_1),
        operation(opcode::codeseparator),
    });

    // run() baseline.
    {
        program prog(scr, dummy_tx(), 0, 0, 0);
        auto const result = interpreter::run(prog);
        REQUIRE(bool(result));
    }

    // debug_run() must agree.
    {
        auto snap = interpreter::debug_run(
            interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
        REQUIRE(snap.done);
        REQUIRE(bool(snap.last));
    }
}

// Pin `active_frame::pc` (the per-frame mirror of the outermost PC
// introduced by the PC-based mark_code_separator refactor). OP_INVOKE a
// function whose body is `OP_CODESEPARATOR OP_ACTIVEBYTECODE`: the
// CODESEPARATOR must anchor jump at the callee frame's post-separator
// position, and the subsequent OP_ACTIVEBYTECODE read must see only
// the bytes after it — i.e. a single `0xc1`. Regresses to the wrong
// script if the frame-local PC isn't updated correctly.
TEST_CASE("OP_CODESEPARATOR inside OP_INVOKE anchors per-frame PC",
          "[interpreter][invoke][codeseparator]") {
    auto const body = data_chunk{
        static_cast<uint8_t>(opcode::codeseparator),
        static_cast<uint8_t>(opcode::active_bytecode),
    };
    auto scr = script_of({
        operation(body),
        operation(opcode::push_size_0),
        operation(opcode::op_define),
        operation(opcode::push_size_0),
        operation(opcode::op_invoke),
    });
    auto const flags = script_flags::bch_subroutines
                     | script_flags::bch_native_introspection;

    program prog(scr, dummy_tx(), 0, flags, 0);
    auto const result = interpreter::run(prog);
    REQUIRE(bool(result));
    // Top of stack is the active bytecode as seen AFTER OP_CODESEPARATOR
    // in the callee's body — just the OP_ACTIVEBYTECODE byte itself.
    REQUIRE(prog.top() == data_chunk{
        static_cast<uint8_t>(opcode::active_bytecode)
    });
}

// On nested `OP_INVOKE` failure, `run_script` propagates the inner
// `op_result` verbatim (line 139, `return inner;`), preserving the
// opcode that actually caused the failure inside the subroutine body.
// A prior revision of `step_one` re-wrapped it as
// `{inner.error, op.code()}`, overwriting the inner opcode with
// `op_invoke` — so `run()` and `debug_run()` disagreed on the
// failure site's attribution for the same script.
TEST_CASE("OP_INVOKE inner-failure attribution agrees between run and debug",
          "[interpreter][debug][parity]") {
    // Define function 0 = `OP_VERIFY`, then invoke it. `OP_VERIFY`
    // with an empty stack fails attributed to `opcode::verify` — not
    // `opcode::op_invoke`.
    auto const body = data_chunk{
        static_cast<uint8_t>(opcode::verify),
    };
    auto scr = script_of({
        operation(body),
        operation(opcode::push_size_0),
        operation(opcode::op_define),
        operation(opcode::push_size_0),
        operation(opcode::op_invoke),
    });
    auto const flags = script_flags::bch_subroutines;

    // run() path.
    program prog_run(scr, dummy_tx(), 0, flags, 0);
    auto const run_result = interpreter::run(prog_run);
    REQUIRE_FALSE(bool(run_result));
    REQUIRE(run_result.op.has_value());
    auto const run_op = *run_result.op;

    // debug_run() path.
    auto snap = interpreter::debug_run(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, flags, 0)));
    REQUIRE(snap.done);
    REQUIRE_FALSE(bool(snap.last));
    REQUIRE(snap.last.op.has_value());
    auto const debug_op = *snap.last.op;

    // Both paths must agree on the failure-site opcode, and the
    // inner opcode (`verify`) — not the outer `op_invoke` — should
    // be surfaced.
    REQUIRE(run_op == debug_op);
    REQUIRE(run_op == opcode::verify);
}

// `op_check_multisig_verify` delegates internal pop/parse failures to
// the same helper as `op_check_multisig`. A prior revision tagged
// every `_internal` failure with `opcode::checkmultisig`, so when
// OP_CHECKMULTISIGVERIFY failed inside the helper the error surfaced
// with the wrong opcode — diverging from what users see in the
// failing script.
TEST_CASE("OP_CHECKMULTISIGVERIFY attributes internal failures to the verify opcode",
          "[interpreter][multisig][attribution]") {
    // Empty stack → pop_int32 inside `op_check_multisig_internal`
    // fails and returns from the helper. Opcode should be
    // checkmultisigverify, not checkmultisig.
    auto scr = script_of({operation(opcode::checkmultisigverify)});
    program prog(scr, dummy_tx(), 0, 0, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::checkmultisigverify);
}

// `top_number()` inside OP_CHECKLOCKTIMEVERIFY and
// OP_CHECKSEQUENCEVERIFY can fail with three distinct error codes
// (`insufficient_main_stack`, `minimal_number`, `invalid_operand_size`).
// A prior revision replaced all of them with a single hardcoded error
// (`invalid_script` for CLTV, `insufficient_main_stack` for CSV),
// masking the actual parse failure.
TEST_CASE("OP_CHECKLOCKTIMEVERIFY propagates top_number parse error",
          "[interpreter][cltv][attribution]") {
    // Empty stack → top_number() returns insufficient_main_stack.
    // Needs a transaction context with at least one input (CLTV
    // reads tx.inputs()[input_index]).
    chain::transaction tx;
    tx.inputs().push_back(chain::input{});
    auto scr = script_of({operation(opcode::checklocktimeverify)});
    program prog(scr, tx, 0, script_flags::bip65_rule, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::checklocktimeverify);
    REQUIRE(result.error == error::insufficient_main_stack);
}

TEST_CASE("OP_CHECKSEQUENCEVERIFY propagates top_number parse error",
          "[interpreter][csv][attribution]") {
    // Push a 6-byte chunk (exceeds CSV's 5-byte script-number cap) so
    // `top_number()` fails via `number::set_data` with
    // `invalid_operand_size`. Run without `bch_minimaldata` so the
    // pre-check at the top of `op_check_sequence_verify` doesn't
    // preempt with its own error.
    chain::transaction tx;
    tx.inputs().push_back(chain::input{});
    auto scr = script_of({
        operation(data_chunk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}),
        operation(opcode::checksequenceverify),
    });
    program prog(scr, tx, 0, script_flags::bip112_rule, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::checksequenceverify);
    REQUIRE(result.error == error::invalid_operand_size);
}

// `run_script` is invoked recursively for every `OP_INVOKE`, and the
// inner call builds its own local `loop_stack`. The per-op VM-limits
// check inside the inner frame uses only that local stack, so any
// `OP_BEGIN` opened in the outer frame is invisible to the inner
// depth accounting. BCHN tracks the sum across all frames via its
// `EvalStack::depth()` (cumulativeCtr + current-frame IFs + loops +
// parent-frame count); kth must do the same or a script that opens
// ~99 outer loops can then spend another ~100 inside a callee without
// tripping `conditional_stack_depth`.
TEST_CASE("Nested OP_INVOKE counts outer OP_BEGIN loops in depth check",
          "[interpreter][invoke][depth][consensus]") {
    // Outer: 99 OP_BEGINs (outer loop depth = 99) then OP_INVOKE.
    // Pre-entry check: 0 IFs + 99 outer loops + 1 invoke_depth = 100,
    // not > 100 — OP_INVOKE proceeds.
    //
    // Callee body: OP_BEGIN OP_1 OP_UNTIL. The per-op check AFTER
    // the inner OP_BEGIN must see:
    //   0 (IFs) + 99 (outer loops) + 1 (inner loop) + 1 (invoke_depth) = 101
    // → fail with `conditional_stack_depth` attributed to the inner
    // OP_BEGIN. Without the fix, the inner check sees only the
    // empty local loop_stack (1, not 100) and the script runs on.
    operation::list ops;
    for (int i = 0; i < 99; ++i) {
        ops.emplace_back(opcode::op_begin);
    }

    // Function 0's body = `OP_BEGIN OP_1 OP_UNTIL` (opens a loop and
    // runs it once).
    data_chunk const body{
        static_cast<uint8_t>(opcode::op_begin),
        static_cast<uint8_t>(opcode::push_positive_1),
        static_cast<uint8_t>(opcode::op_until),
    };
    ops.emplace_back(body);
    ops.emplace_back(opcode::push_size_0);
    ops.emplace_back(opcode::op_define);

    // Invoke function 0.
    ops.emplace_back(opcode::push_size_0);
    ops.emplace_back(opcode::op_invoke);
    auto scr = chain::script(std::move(ops));

    auto const flags = script_flags::bch_vm_limits
                     | script_flags::bch_loops
                     | script_flags::bch_subroutines;

    program prog(scr, dummy_tx(), 0, flags, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.op.has_value());
    REQUIRE(*result.op == opcode::op_begin);
    REQUIRE(result.error == error::conditional_stack_depth);
}

// `debug_step_until` is the debugger's breakpoint primitive. It must
// evaluate the predicate before taking any step, otherwise a predicate
// that's already true on the initial snapshot would still drive one op
// forward — breakpoints set on the entry state become unreachable.
TEST_CASE("debug_step_until honours a predicate true at entry",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_positive_1),
        operation(opcode::push_positive_1),
    });
    auto start = interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0));
    REQUIRE(start.step == 0);

    // Predicate already matches the initial snapshot. Expected: no
    // step taken; snapshot returned unchanged.
    auto stopped = interpreter::debug_step_until(
        std::move(start),
        [](debug_snapshot const& s) { return s.step == 0; });
    REQUIRE(stopped.step == 0);
    REQUIRE_FALSE(stopped.done);
}

// ---------------------------------------------------------------------------
// debug_finalize
// ---------------------------------------------------------------------------

TEST_CASE("debug_finalize on a closed snapshot returns success",
          "[interpreter][debug]") {
    auto scr = script_of({operation(opcode::push_positive_1)});
    auto snap = interpreter::debug_run(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    auto const result = interpreter::debug_finalize(snap);
    REQUIRE(bool(result));
}

TEST_CASE("debug_finalize propagates an earlier error",
          "[interpreter][debug]") {
    auto scr = script_of({operation(opcode::verify)});
    auto snap = interpreter::debug_run(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    auto const result = interpreter::debug_finalize(snap);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.error != error::success);
}

TEST_CASE("debug_finalize refuses unfinished snapshots",
          "[interpreter][debug]") {
    auto scr = script_of({
        operation(opcode::push_size_0),
        operation(opcode::push_size_0),
    });
    // Caller stepped once out of two — script is not yet done.
    auto snap = interpreter::debug_step(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE_FALSE(snap.done);
    auto const result = interpreter::debug_finalize(snap);
    REQUIRE_FALSE(bool(result));
}

TEST_CASE("debug_begin on an empty script marks the snapshot done",
          "[interpreter][debug]") {
    auto scr = script_of({});
    auto const snap = interpreter::debug_begin(
        program(scr, dummy_tx(), 0, 0, 0));
    REQUIRE(snap.done);
    // Caller can still ask for the final outcome; an empty, closed
    // script is trivially valid.
    REQUIRE(bool(interpreter::debug_finalize(snap)));
}

// ---------------------------------------------------------------------------
// Parity-affecting checks step_one mirrors from run()
// ---------------------------------------------------------------------------

TEST_CASE("debug_step on OP_BEGIN without bch_loops flag errors with op_reserved",
          "[interpreter][debug]") {
    // OP_BEGIN is a May 2026 opcode; without the matching script flag
    // it should surface as a reserved-op error, same as `run()`.
    auto scr = script_of({operation(opcode::op_begin)});
    auto const snap = interpreter::debug_step(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE_FALSE(bool(snap.last));
    REQUIRE(snap.last.error == error::op_reserved);
    REQUIRE(snap.last.op.has_value());
    REQUIRE(*snap.last.op == opcode::op_begin);
    REQUIRE(snap.done);
}

TEST_CASE("debug_step on OP_INVOKE without bch_subroutines flag errors with op_reserved",
          "[interpreter][debug]") {
    auto scr = script_of({operation(opcode::op_invoke)});
    auto const snap = interpreter::debug_step(
        interpreter::debug_begin(program(scr, dummy_tx(), 0, 0, 0)));
    REQUIRE_FALSE(bool(snap.last));
    REQUIRE(snap.last.error == error::op_reserved);
}

TEST_CASE("debug_step drives OP_BEGIN / OP_UNTIL loop with bch_loops enabled",
          "[interpreter][debug]") {
    // PUSH 1  (truthy condition)
    // OP_BEGIN
    // OP_UNTIL   — pops the 1, condition true, exits the loop immediately.
    auto scr = script_of({
        operation(opcode::push_positive_1),
        operation(opcode::op_begin),
        operation(opcode::op_until),
    });
    auto const snap = interpreter::debug_run(
        interpreter::debug_begin(
            program(scr, dummy_tx(), 0, script_flags::bch_loops, 0)));
    REQUIRE(snap.done);
    REQUIRE(bool(snap.last));
    REQUIRE(snap.loop_stack.empty());
    REQUIRE(snap.control_stack.empty());
}
