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

#include <kth/capi/chain/opcode.h>
#include <kth/capi/chain/operation.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/interpreter.h>
#include <kth/capi/vm/program.h>

#include "../test_helpers.hpp"

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
    kth_error_code_t const ec = kth_vm_interpreter_run_operation(op, prog);
    kth_chain_operation_destruct(op);
    kth_vm_program_destruct(prog);

    REQUIRE(ec == kth_ec_not_implemented);
}
