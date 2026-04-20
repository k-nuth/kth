// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is named .cpp solely so it can use Catch2 (which is C++).
// Everything inside the test bodies is plain C: no namespaces, no
// templates, no <chrono>, no std::*, no auto, no references, no constexpr.
// Only Catch2's TEST_CASE / REQUIRE macros are C++. The point is that
// these tests must exercise the C-API exactly the way a C consumer would.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>

#include <kth/capi/bool_list.h>
#include <kth/capi/chain/opcode.h>
#include <kth/capi/chain/operation.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/debug_snapshot.h>
#include <kth/capi/vm/debug_snapshot_list.h>
#include <kth/capi/vm/interpreter.h>
#include <kth/capi/vm/program.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixture — tiny script used by the debug-API smoke tests below
// ---------------------------------------------------------------------------

// Two-op script: `OP_1 OP_1` (bytes 0x51, 0x51). After running, the
// stack holds two truthy elements; `debug_finalize` should report
// success.
static uint8_t const kTwoPushes[2] = { 0x51, 0x51 };

static void build_two_push_program(kth_program_mut_t* out_prog,
                                   kth_script_mut_t* out_script) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kTwoPushes, sizeof(kTwoPushes), 0, &script);
    REQUIRE(ec == kth_ec_success);
    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);
    REQUIRE(prog != NULL);
    *out_prog = prog;
    *out_script = script;
}

// ---------------------------------------------------------------------------
// run_operation — OP_CODESEPARATOR has no standalone semantics
// ---------------------------------------------------------------------------

TEST_CASE("C-API Interpreter - run_operation rejects OP_CODESEPARATOR standalone",
          "[C-API Interpreter][run_operation][codeseparator]") {
    // OP_CODESEPARATOR anchors the active-bytecode-hash marker at
    // (script position + 1), and the single-op driver has no script
    // position to anchor to. The binding must surface
    // `kth_ec_not_implemented` rather than fabricate a position —
    // the full-script runner handles OP_CODESEPARATOR directly via
    // `kth_vm_program_mark_code_separator(prog, idx)`.
    kth_program_mut_t prog = kth_vm_program_construct_default();
    kth_operation_mut_t op = kth_chain_operation_construct_from_code(
        kth_opcode_codeseparator);

    // Capture the verdict before asserting. A failing REQUIRE aborts
    // the test body immediately (Catch2 is exception-based), so any
    // destructor call after the REQUIRE would be skipped and leak
    // the handles; release first, then assert.
    kth_error_code_t const ec = kth_vm_interpreter_run(op, prog);
    kth_chain_operation_destruct(op);
    kth_vm_program_destruct(prog);

    REQUIRE(ec == kth_ec_not_implemented);
}

// ---------------------------------------------------------------------------
// debug_begin + snapshot accessors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Interpreter - debug_begin yields a fresh snapshot at step 0",
          "[C-API Interpreter][debug]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t snap = kth_vm_interpreter_debug_begin(prog);
    REQUIRE(snap != NULL);
    REQUIRE(kth_vm_debug_snapshot_step(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_done(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_last(snap) == kth_ec_success);

    kth_vm_debug_snapshot_destruct(snap);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// debug_step / debug_step_n
// ---------------------------------------------------------------------------

TEST_CASE("C-API Interpreter - debug_step advances one op at a time",
          "[C-API Interpreter][debug]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t s0 = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t s1 = kth_vm_interpreter_debug_step(s0);
    REQUIRE(kth_vm_debug_snapshot_step(s1) == 1);
    REQUIRE(kth_vm_debug_snapshot_done(s1) == 0);

    kth_debug_snapshot_mut_t s2 = kth_vm_interpreter_debug_step(s1);
    REQUIRE(kth_vm_debug_snapshot_step(s2) == 2);
    REQUIRE(kth_vm_debug_snapshot_done(s2) != 0);

    kth_vm_debug_snapshot_destruct(s2);
    kth_vm_debug_snapshot_destruct(s1);
    kth_vm_debug_snapshot_destruct(s0);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API Interpreter - debug_step_n advances by N or stops at done",
          "[C-API Interpreter][debug]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t s0 = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t s_many = kth_vm_interpreter_debug_step_n(s0, 10);
    REQUIRE(kth_vm_debug_snapshot_done(s_many) != 0);

    kth_vm_debug_snapshot_destruct(s_many);
    kth_vm_debug_snapshot_destruct(s0);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// debug_run + debug_finalize
// ---------------------------------------------------------------------------

TEST_CASE("C-API Interpreter - debug_run + debug_finalize reach success",
          "[C-API Interpreter][debug]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t s0 = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t done = kth_vm_interpreter_debug_run(s0);
    REQUIRE(kth_vm_debug_snapshot_done(done) != 0);
    REQUIRE(kth_vm_interpreter_debug_finalize(done) == kth_ec_success);

    kth_vm_debug_snapshot_destruct(done);
    kth_vm_debug_snapshot_destruct(s0);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// debug_run_traced
// ---------------------------------------------------------------------------

TEST_CASE("C-API Interpreter - debug_run_traced records initial + per-step snapshots",
          "[C-API Interpreter][debug]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t start = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_list_mut_t trace = kth_vm_interpreter_debug_run_traced(start);
    REQUIRE(trace != NULL);
    // Initial + 2 step snapshots = 3 entries.
    REQUIRE(kth_vm_debug_snapshot_list_count(trace) == 3);

    kth_vm_debug_snapshot_list_destruct(trace);
    kth_vm_debug_snapshot_destruct(start);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// debug_step_until (callback)
// ---------------------------------------------------------------------------

// Predicate: stop when `step == *target`. `user_data` carries the
// target step index, mirroring the usual "threaded state" pattern
// for C callbacks.
static kth_bool_t stop_at_step(kth_debug_snapshot_const_t snap, void* user_data) {
    kth_size_t const target = *(kth_size_t const*)user_data;
    return kth_vm_debug_snapshot_step(snap) == target ? 1 : 0;
}

TEST_CASE("C-API Interpreter - debug_step_until stops when predicate fires",
          "[C-API Interpreter][debug][callback]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t start = kth_vm_interpreter_debug_begin(prog);
    kth_size_t target = 1;
    kth_debug_snapshot_mut_t stopped = kth_vm_interpreter_debug_step_until(
        start, stop_at_step, &target);
    REQUIRE(kth_vm_debug_snapshot_step(stopped) == 1);
    // Predicate caught us mid-execution — not done yet.
    REQUIRE(kth_vm_debug_snapshot_done(stopped) == 0);

    kth_vm_debug_snapshot_destruct(stopped);
    kth_vm_debug_snapshot_destruct(start);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}
