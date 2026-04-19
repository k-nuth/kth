// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_INTERPRETER_HPP
#define KTH_DOMAIN_MACHINE_INTERPRETER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

// Outcome of a single opcode (or of a full script run).
//
// `op` is the offending opcode when an error was raised by that
// opcode — empty on success, and empty for "structural" errors that
// aren't attributable to a specific opcode (end-of-script without
// closing the conditional stack, invalid script input, ...).
//
// `operator bool()` follows the standard-library convention:
// `true` means success. An older revision of this type had it
// inverted; callers should read `if (result) { /* ok */ }`.
struct op_result {
    error::error_code_t error = error::success;
    std::optional<opcode> op;

    constexpr op_result() = default;
    // Single-arg `op_result(error_code_t)` is intentionally absent —
    // every error site MUST attach the offending opcode. Absence of
    // the implicit ctor means the compiler flags any un-audited site
    // so we either pass the opcode explicitly or opt in through the
    // `std::nullopt` overload for the genuinely-not-attributable
    // structural errors.
    constexpr op_result(error::error_code_t e, opcode o) : error(e), op(o) {}
    constexpr op_result(error::error_code_t e, std::nullopt_t) : error(e) {}

    // Explicit factory for the success case. Success is not tied to
    // a particular opcode, so the dedicated entry point spares call
    // sites from picking a placeholder value. Backed by the private
    // single-arg ctor so external code can't silently smuggle an
    // `error_code_t` through it and drop the opcode info.
    static constexpr op_result ok() {
        return op_result{error::success};
    }

    // Named state predicates — explicit intent at call sites instead
    // of relying only on `operator bool()`. Pair kept symmetric
    // (`is_ok` / `is_err`) so both spellings read naturally.
    [[nodiscard]] constexpr bool is_ok()  const { return error == error::success; }
    [[nodiscard]] constexpr bool is_err() const { return error != error::success; }

    constexpr explicit operator bool() const { return is_ok(); }

    // Explicit conversion to `kth::code` for interop with legacy call
    // sites. Kept `explicit` on purpose: `op_result::operator bool()`
    // is success→true while `code::operator bool()` is error→true, so
    // an implicit chain would silently flip the polarity of any
    // boolean test performed on an `auto`-deduced value
    // (e.g. `auto const ec = interpreter::run(prog); if (ec) { ... }`).
    // Forcing the conversion at the call site — via direct-init
    // `code ec{run(prog)}` or explicit `ec.error` access — makes the
    // bridge visible and audit-able.
    explicit operator code() const { return error; }

private:
    // Used by `ok()` — restricted to that factory on purpose.
    constexpr explicit op_result(error::error_code_t e) : error(e) {}
};

// Immutable snapshot of an in-progress script execution. Each
// `debug_step` call consumes a snapshot by value and returns a new
// one, so the caller can keep a `std::vector<debug_snapshot>` around
// and rewind to any prior point — forward replay is deterministic,
// true reverse execution is not (hash / EC / arithmetic-with-overflow
// ops are not invertible), so the snapshot-history approach is the
// only practical way to go "back" in a Bitcoin-script debugger.
struct debug_snapshot {
    program prog;
    // Zero-based index of the next operation to execute (the
    // program counter in debug terms; `program::jump()` is unrelated
    // — that one tracks OP_CODESEPARATOR's position).
    size_t step = 0;
    op_result last;        // outcome of the step that produced this snapshot
    bool done = false;     // no more steps — either finished or errored

    // IF / ELSE / ENDIF / loop bookkeeping kept across steps so the
    // debugger catches the same structural errors `run()` raises.
    // `true` = OP_BEGIN frame (loop), `false` = IF block.
    std::vector<bool> control_stack;
    // Backward-jump targets for OP_UNTIL; the index of the op
    // immediately after the matching OP_BEGIN.
    std::vector<size_t> loop_stack;
    // Shared function table populated by OP_DEFINE and consumed by
    // OP_INVOKE. Lives on the snapshot so defines accumulate across
    // steps the same way they do inside a run() call.
    boost::unordered_flat_map<data_chunk, data_chunk> function_table;
    // Nesting level for OP_INVOKE — contributes to the conditional
    // stack depth limit.
    size_t invoke_depth = 0;
    // Cumulative `loop_stack.size()` from every enclosing frame. Zero
    // at the outermost snapshot (what `debug_begin` produces). Today
    // `step_one` runs only at the outermost level so this is always
    // zero in practice; the field is threaded defensively so a
    // future step-INTO feature (see TODO: "Debugger step into / step
    // out — wait for BCH 2026-May subroutines") can set it without a
    // second round of plumbing.
    size_t outer_loop_depth = 0;

    debug_snapshot() = default;
    // `explicit` so a stray `debug_step(prog)` can't silently convert
    // a `program` into a fresh snapshot and bypass the validation
    // that `debug_begin` performs (script-size, VM-limits init, ...).
    explicit debug_snapshot(machine::program p) : prog(std::move(p)) {}
};

struct KD_API interpreter {
    using result = op_result;

    // Operations (shared).
    //-----------------------------------------------------------------------------

    static
    result op_nop(opcode /*unused*/);

    static
    result op_disabled(opcode /*unused*/);

    static
    result op_reserved(opcode /*unused*/);

    static
    result op_push_number(program& program, uint8_t value);

    static
    result op_push_size(program& program, operation const& op);

    static
    result op_push_data(program& program, opcode code, data_chunk const& data, uint32_t size_limit);

    // Operations (not shared).
    //-----------------------------------------------------------------------------

    static
    result op_if(program& program);

    static
    result op_notif(program& program);

    static
    result op_else(program& program);

    static
    result op_endif(program& program);

    static
    result op_verify(program& program);

    static
    result op_return(program& program);

    static
    result op_to_alt_stack(program& program);

    static
    result op_from_alt_stack(program& program);

    static
    result op_drop2(program& program);

    static
    result op_dup2(program& program);

    static
    result op_dup3(program& program);

    static
    result op_over2(program& program);

    static
    result op_rot2(program& program);

    static
    result op_swap2(program& program);

    static
    result op_if_dup(program& program);

    static
    result op_depth(program& program);

    static
    result op_drop(program& program);

    static
    result op_dup(program& program);

    static
    result op_nip(program& program);

    static
    result op_over(program& program);

    static
    result op_pick(program& program);

    static
    result op_roll(program& program);

    static
    result op_rot(program& program);

    static
    result op_swap(program& program);

    static
    result op_tuck(program& program);

    static
    result op_cat(program& program);

    static
    result op_split(program& program);

    static
    result op_reverse_bytes(program& program);

    static
    result op_num2bin(program& program);

    static
    result op_bin2num(program& program);

    static
    result op_size(program& program);

    static
    result op_invert(program& program);

    static
    result op_and(program& program);

    static
    result op_or(program& program);

    static
    result op_xor(program& program);

    static
    result op_equal(program& program);

    static
    result op_equal_verify(program& program);

    static
    result op_add1(program& program);

    static
    result op_sub1(program& program);

    static
    result op_negate(program& program);

    static
    result op_abs(program& program);

    static
    result op_not(program& program);

    static
    result op_nonzero(program& program);

    static
    result op_add(program& program);

    static
    result op_sub(program& program);

    static
    result op_mul(program& program);

    static
    result op_div(program& program);

    static
    result op_mod(program& program);

    static
    result op_shiftnum(program& program, opcode code);

    static
    result op_shiftbin(program& program, opcode code);

    static
    result op_bool_and(program& program);

    static
    result op_bool_or(program& program);

    static
    result op_num_equal(program& program);

    static
    result op_num_equal_verify(program& program);

    static
    result op_num_not_equal(program& program);

    static
    result op_less_than(program& program);

    static
    result op_greater_than(program& program);

    static
    result op_less_than_or_equal(program& program);

    static
    result op_greater_than_or_equal(program& program);

    static
    result op_min(program& program);

    static
    result op_max(program& program);

    static
    result op_within(program& program);

    static
    result op_ripemd160(program& program);

    static
    result op_sha1(program& program);

    static
    result op_sha256(program& program);

    static
    result op_hash160(program& program);

    static
    result op_hash256(program& program);

    static
    result op_codeseparator(program& program, operation const& op);

    static
    result op_check_sig(program& program);

    static
    result op_check_sig_verify(program& program);

    static
    result op_check_data_sig(program& program);

    static
    result op_check_data_sig_verify(program& program);

    static
    result op_check_multisig_verify(program& program);

    static
    result op_check_multisig(program& program);

    static
    result op_check_locktime_verify(program& program);

    static
    result op_check_sequence_verify(program& program);

    static
    result op_input_index(program& program);

    static
    result op_active_bytecode(program& program);

    static
    result op_tx_version(program& program);

    static
    result op_tx_input_count(program& program);

    static
    result op_tx_output_count(program& program);

    static
    result op_tx_locktime(program& program);

    static
    result op_utxo_value(program& program);

    static
    result op_utxo_bytecode(program& program);

    static
    result op_outpoint_tx_hash(program& program);

    static
    result op_outpoint_index(program& program);

    static
    result op_input_bytecode(program& program);

    static
    result op_input_sequence_number(program& program);

    static
    result op_output_value(program& program);

    static
    result op_output_bytecode(program& program);

    static
    result op_utxo_token_category(program& program);

    static
    result op_utxo_token_commitment(program& program);

    static
    result op_utxo_token_amount(program& program);

    static
    result op_output_token_category(program& program);

    static
    result op_output_token_commitment(program& program);

    static
    result op_output_token_amount(program& program);

    /// Run program script end-to-end. On failure, `op_result::op`
    /// names the opcode that raised the error (when one is
    /// attributable — end-of-script structural errors leave it empty).
    static
    op_result run(program& program);

    /// Run a single operation outside of the script driver. Prefer
    /// `run(program&)` for a full script — this overload is for tests
    /// and the C-API step-one-op path.
    static
    op_result run(operation const& op, program& program);


// Debug step by step
// ----------------------------------------------------------------------------
//
// All debug primitives take `debug_snapshot` by value so each call
// naturally produces a new snapshot; the caller keeps prior
// snapshots to rewind. `snapshot.step` is the program counter;
// `snapshot.prog.size()` / `snapshot.prog.get_metrics()` and the
// other `program` observers expose the live execution state.

    /// Open a debug session. Validates the program and returns an
    /// initial snapshot at step 0.
    static
    debug_snapshot debug_begin(program prog);

    /// Execute one op, return the new snapshot. Caller keeps the
    /// prior snapshot if they want to rewind.
    static
    debug_snapshot debug_step(debug_snapshot snapshot);

    /// Execute up to `n` steps (or until done / error).
    static
    debug_snapshot debug_step_n(debug_snapshot snapshot, size_t n);

    /// Step until the predicate returns `true` on the post-step
    /// snapshot (or until done / error). Covers the usual debugger
    /// breakpoints: by PC, by opcode, on error, on stack-depth
    /// threshold, etc.
    static
    debug_snapshot debug_step_until(
        debug_snapshot snapshot,
        std::function<bool(debug_snapshot const&)> const& predicate);

    /// Run to completion without stopping.
    static
    debug_snapshot debug_run(debug_snapshot snapshot);

    /// Run to completion and return every post-step snapshot,
    /// including the initial one. Use to record a full trace for
    /// rewind / display / analysis. Expensive for long scripts;
    /// prefer `debug_run` when you only need the final state.
    static
    std::vector<debug_snapshot> debug_run_traced(debug_snapshot start);

    /// Validate the "script closed" post-condition and return the
    /// final result. Returns the earlier error if the snapshot
    /// already recorded one.
    static
    op_result debug_finalize(debug_snapshot const& snapshot);

private:
    static
    result run_op(operation const& op, program& program);

    /// Helper function for common Native Introspection validations
    static
    result validate_native_introspection(program const& program, opcode op_code);

    /// Helper function for Token Introspection validations (requires bch_tokens)
    static
    result validate_token_introspection(program const& program, opcode op_code);

    /// Helper function for post-processing Native Introspection push operations
    static
    void post_process_introspection_push(program& program, data_chunk const& data);
};

} // namespace kth::domain::machine

#include <kth/domain/impl/machine/interpreter.ipp>

#endif
