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
#include <kth/capi/chain/script.h>
#include <kth/capi/primitives.h>
#include <kth/capi/u64_list.h>
#include <kth/capi/vm/debug_snapshot.h>
#include <kth/capi/vm/interpreter.h>
#include <kth/capi/vm/program.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Two-op script: `OP_1 OP_1` (bytes 0x51, 0x51). After running, the
// stack holds two truthy elements; `debug_finalize` reports success.
static uint8_t const kTwoPushes[2] = { 0x51, 0x51 };

static void build_two_push_program(kth_program_mut_t* out_prog,
                                   kth_script_mut_t* out_script) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kTwoPushes, sizeof(kTwoPushes), 0, &script);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(script != NULL);

    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);
    REQUIRE(prog != NULL);

    *out_prog = prog;
    *out_script = script;
}

// ---------------------------------------------------------------------------
// Lifecycle — construct_default / construct(program) / copy / destruct
// ---------------------------------------------------------------------------

TEST_CASE("C-API DebugSnapshot - default construct yields a valid zero-step snapshot",
          "[C-API DebugSnapshot][lifecycle]") {
    kth_debug_snapshot_mut_t snap = kth_vm_debug_snapshot_construct_default();
    REQUIRE(snap != NULL);
    REQUIRE(kth_vm_debug_snapshot_step(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_done(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_last(snap) == kth_ec_success);
    REQUIRE(kth_vm_debug_snapshot_invoke_depth(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_outer_loop_depth(snap) == 0);

    kth_vm_debug_snapshot_destruct(snap);
}

TEST_CASE("C-API DebugSnapshot - construct(program) seats the snapshot on the script",
          "[C-API DebugSnapshot][lifecycle]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t snap = kth_vm_debug_snapshot_construct(prog);
    REQUIRE(snap != NULL);
    REQUIRE(kth_vm_debug_snapshot_step(snap) == 0);
    REQUIRE(kth_vm_debug_snapshot_done(snap) == 0);
    // The `prog` getter returns a borrowed view — must not be destructed.
    REQUIRE(kth_vm_debug_snapshot_prog(snap) != NULL);

    kth_vm_debug_snapshot_destruct(snap);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API DebugSnapshot - destruct(NULL) is a no-op",
          "[C-API DebugSnapshot][lifecycle]") {
    kth_vm_debug_snapshot_destruct(NULL);
}

TEST_CASE("C-API DebugSnapshot - copy produces an independent snapshot",
          "[C-API DebugSnapshot][lifecycle]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t s0 = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t s1 = kth_vm_interpreter_debug_step(s0);
    REQUIRE(kth_vm_debug_snapshot_step(s1) == 1);

    // Copy the advanced snapshot; the copy reports the same step but is
    // a distinct heap object (destruct of one must not affect the other).
    kth_debug_snapshot_mut_t cp = kth_vm_debug_snapshot_copy(s1);
    REQUIRE(cp != NULL);
    REQUIRE(kth_vm_debug_snapshot_step(cp) == 1);

    kth_vm_debug_snapshot_destruct(cp);
    // Original still usable after the copy was destructed.
    REQUIRE(kth_vm_debug_snapshot_step(s1) == 1);

    kth_vm_debug_snapshot_destruct(s1);
    kth_vm_debug_snapshot_destruct(s0);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Rewind semantics — `debug_step` returns a NEW snapshot, input untouched
// ---------------------------------------------------------------------------

TEST_CASE("C-API DebugSnapshot - debug_step preserves the input snapshot (rewind-friendly)",
          "[C-API DebugSnapshot][rewind]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t start = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t advanced = kth_vm_interpreter_debug_step(start);

    // Mirrors the domain-level `debug_step preserves the input
    // snapshot` case: stepping does not mutate the caller's snapshot,
    // so the caller can fork execution off any prior step.
    REQUIRE(kth_vm_debug_snapshot_step(start) == 0);
    REQUIRE(kth_vm_debug_snapshot_step(advanced) == 1);

    kth_vm_debug_snapshot_destruct(advanced);
    kth_vm_debug_snapshot_destruct(start);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

TEST_CASE("C-API DebugSnapshot - debug_step on a done snapshot is a no-op",
          "[C-API DebugSnapshot][rewind]") {
    // Single-op script: after one step the snapshot is `done`.
    uint8_t const one_push[1] = { 0x51 };
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        one_push, sizeof(one_push), 0, &script);
    REQUIRE(ec == kth_ec_success);
    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);

    kth_debug_snapshot_mut_t s0 = kth_vm_interpreter_debug_begin(prog);
    kth_debug_snapshot_mut_t s1 = kth_vm_interpreter_debug_step(s0);
    REQUIRE(kth_vm_debug_snapshot_done(s1) != 0);
    kth_size_t const before = kth_vm_debug_snapshot_step(s1);

    // Stepping a done snapshot produces another done snapshot with the
    // same step index — mirrors the domain-level "no-op on done" case.
    kth_debug_snapshot_mut_t s2 = kth_vm_interpreter_debug_step(s1);
    REQUIRE(kth_vm_debug_snapshot_done(s2) != 0);
    REQUIRE(kth_vm_debug_snapshot_step(s2) == before);

    kth_vm_debug_snapshot_destruct(s2);
    kth_vm_debug_snapshot_destruct(s1);
    kth_vm_debug_snapshot_destruct(s0);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Getter — borrowed `control_stack` (bool_list) is queryable
// ---------------------------------------------------------------------------

TEST_CASE("C-API DebugSnapshot - control_stack getter returns a queryable bool_list view",
          "[C-API DebugSnapshot][getter]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t snap = kth_vm_interpreter_debug_begin(prog);

    // The control stack on a freshly-begun script is empty. The
    // borrowed list view is valid to query for size; we do not
    // destruct it — the parent snapshot owns the underlying storage.
    kth_bool_list_const_t cs = kth_vm_debug_snapshot_control_stack(snap);
    REQUIRE(cs != NULL);
    REQUIRE(kth_core_bool_list_count(cs) == 0);

    kth_vm_debug_snapshot_destruct(snap);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Getter — `loop_stack` returns an owned u64_list the caller must release
// ---------------------------------------------------------------------------

TEST_CASE("C-API DebugSnapshot - loop_stack getter returns an owned u64_list",
          "[C-API DebugSnapshot][getter]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_two_push_program(&prog, &script);

    kth_debug_snapshot_mut_t snap = kth_vm_interpreter_debug_begin(prog);

    // No OP_BEGIN / OP_UNTIL in the fixture → empty loop stack.
    // Unlike control_stack, loop_stack returns an OWNED u64_list that
    // the caller must release with `kth_core_u64_list_destruct`.
    kth_u64_list_mut_t ls = kth_vm_debug_snapshot_loop_stack(snap);
    REQUIRE(ls != NULL);
    REQUIRE(kth_core_u64_list_count(ls) == 0);
    kth_core_u64_list_destruct(ls);

    kth_vm_debug_snapshot_destruct(snap);
    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Preconditions — const getters abort on NULL snapshot
// ---------------------------------------------------------------------------

TEST_CASE("C-API DebugSnapshot - step null aborts",
          "[C-API DebugSnapshot][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_debug_snapshot_step(NULL));
}

TEST_CASE("C-API DebugSnapshot - done null aborts",
          "[C-API DebugSnapshot][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_debug_snapshot_done(NULL));
}

TEST_CASE("C-API DebugSnapshot - last null aborts",
          "[C-API DebugSnapshot][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_debug_snapshot_last(NULL));
}

TEST_CASE("C-API DebugSnapshot - prog null aborts",
          "[C-API DebugSnapshot][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_debug_snapshot_prog(NULL));
}

TEST_CASE("C-API DebugSnapshot - copy null aborts",
          "[C-API DebugSnapshot][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_debug_snapshot_copy(NULL));
}
