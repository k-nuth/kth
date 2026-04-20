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

#include <kth/capi/chain/script.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/program.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Two-op script: `OP_1 OP_CODESEPARATOR` (bytes 0x51, 0xab). Valid op
// indices are {0, 1}; `mark_code_separator(pc)` anchors the active-
// bytecode-hash marker at `pc + 1`, which must fall inside [0, size].
static uint8_t const kTwoOpScript[2] = { 0x51, 0xab };

// Build a program + its backing script. `program` stores a POINTER
// to the script, so the script MUST outlive the program — caller is
// responsible for destructing both, program first. Both outputs are
// returned so the test body can keep them in scope until teardown.
static void build_program_for_two_op_script(kth_program_mut_t* out_prog,
                                            kth_script_mut_t* out_script) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kTwoOpScript, sizeof(kTwoOpScript), 0, &script);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(script != NULL);

    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);
    REQUIRE(prog != NULL);

    *out_prog = prog;
    *out_script = script;
}

// ---------------------------------------------------------------------------
// mark_code_separator — empty script
// ---------------------------------------------------------------------------

TEST_CASE("C-API Program - mark_code_separator on default (empty script) fails",
          "[C-API Program][mark_code_separator]") {
    // `construct_default` builds a program with no bound script. No
    // valid PC exists, so every call must refuse.
    kth_program_mut_t prog = kth_vm_program_construct_default();
    REQUIRE(kth_vm_program_mark_code_separator(prog, 0) == 0);
    kth_vm_program_destruct(prog);
}

// ---------------------------------------------------------------------------
// mark_code_separator — in-range PC
// ---------------------------------------------------------------------------

TEST_CASE("C-API Program - mark_code_separator succeeds for every valid PC",
          "[C-API Program][mark_code_separator]") {
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_program_for_two_op_script(&prog, &script);

    // PC 0 → target (0 + 1) = 1, inside [0, size] → success.
    REQUIRE(kth_vm_program_mark_code_separator(prog, 0) != 0);

    // PC 1 (last valid op index) → target = size = end() iterator.
    // Still a valid destination: the active-bytecode-hash loop reads
    // `[jump, end)` and handles `jump == end` as "no more ops".
    REQUIRE(kth_vm_program_mark_code_separator(prog, 1) != 0);

    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// mark_code_separator — out-of-range PC is guarded (no UB)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Program - mark_code_separator rejects out-of-range PC",
          "[C-API Program][mark_code_separator]") {
    // `pc` is a `size_t` coming straight from the C caller. Anything
    // past the last valid op index would otherwise make
    // `begin + (pc + 1)` form an out-of-bounds iterator (UB). The
    // runtime guard must return 0 instead.
    kth_program_mut_t prog = NULL;
    kth_script_mut_t script = NULL;
    build_program_for_two_op_script(&prog, &script);

    // PC 2 == size → target (2 + 1) = 3, past end() → must reject.
    REQUIRE(kth_vm_program_mark_code_separator(prog, 2) == 0);

    // Huge PC value must also be rejected without UB.
    REQUIRE(kth_vm_program_mark_code_separator(prog, (kth_size_t)(~(kth_size_t)0)) == 0);

    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Null-handle precondition
// ---------------------------------------------------------------------------

TEST_CASE("C-API Program - mark_code_separator null handle aborts",
          "[C-API Program][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_program_mark_code_separator(NULL, 0));
}
