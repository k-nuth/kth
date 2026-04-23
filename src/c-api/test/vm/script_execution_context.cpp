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

#include <kth/capi/chain/input.h>
#include <kth/capi/chain/input_list.h>
#include <kth/capi/chain/output.h>
#include <kth/capi/chain/output_list.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/program.h>
#include <kth/capi/vm/script_execution_context.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — minimal one-in/one-out transaction so the context's
// getters have something non-trivial to return.
// ---------------------------------------------------------------------------

static uint32_t const kVersion  = 2u;
static uint32_t const kLocktime = 42u;

static kth_hash_t const kPrevHash = {{
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf
}};

static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_script_mut_t make_script(void) {
    kth_script_mut_t s = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &s);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(s != NULL);
    return s;
}

static kth_input_list_mut_t make_inputs(void) {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kPrevHash, 0);
    kth_script_mut_t script = make_script();
    kth_input_mut_t in = kth_chain_input_construct(op, script, 0xffffffffu);
    kth_chain_input_list_push_back(list, in);
    kth_chain_input_destruct(in);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
    return list;
}

static kth_output_list_mut_t make_outputs(void) {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    kth_script_mut_t script = make_script();
    kth_output_mut_t out = kth_chain_output_construct(50000ull, script, NULL);
    kth_chain_output_list_push_back(list, out);
    kth_chain_output_destruct(out);
    kth_chain_script_destruct(script);
    return list;
}

static kth_transaction_mut_t make_tx(void) {
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    kth_transaction_mut_t tx = kth_chain_transaction_construct_from_version_locktime_inputs_outputs(
        kVersion, kLocktime, ins, outs);
    REQUIRE(tx != NULL);
    kth_chain_input_list_destruct(ins);
    kth_chain_output_list_destruct(outs);
    return tx;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API ScriptExecutionContext - construct surfaces the input index and tx getters",
          "[C-API ScriptExecutionContext][lifecycle]") {
    kth_transaction_mut_t tx = make_tx();

    kth_script_execution_context_mut_t ctx =
        kth_vm_script_execution_context_construct(0u, tx);
    REQUIRE(ctx != NULL);

    REQUIRE(kth_vm_script_execution_context_input_index(ctx) == 0u);
    REQUIRE(kth_vm_script_execution_context_input_count(ctx) == 1u);
    REQUIRE(kth_vm_script_execution_context_output_count(ctx) == 1u);
    REQUIRE(kth_vm_script_execution_context_tx_version(ctx) == kVersion);
    REQUIRE(kth_vm_script_execution_context_tx_locktime(ctx) == kLocktime);

    kth_vm_script_execution_context_destruct(ctx);
    kth_chain_transaction_destruct(tx);
}

TEST_CASE("C-API ScriptExecutionContext - destruct(NULL) is a no-op",
          "[C-API ScriptExecutionContext][lifecycle]") {
    kth_vm_script_execution_context_destruct(NULL);
}

TEST_CASE("C-API ScriptExecutionContext - copy is an independent handle",
          "[C-API ScriptExecutionContext][lifecycle]") {
    kth_transaction_mut_t tx = make_tx();

    kth_script_execution_context_mut_t orig =
        kth_vm_script_execution_context_construct(0u, tx);
    kth_script_execution_context_mut_t cp =
        kth_vm_script_execution_context_copy(orig);
    REQUIRE(cp != NULL);
    REQUIRE(cp != orig);

    // Destroying the copy must not disturb the source.
    kth_vm_script_execution_context_destruct(cp);
    REQUIRE(kth_vm_script_execution_context_input_index(orig) == 0u);

    kth_vm_script_execution_context_destruct(orig);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Borrowed transaction view
// ---------------------------------------------------------------------------

TEST_CASE("C-API ScriptExecutionContext - transaction() returns a queryable borrowed view",
          "[C-API ScriptExecutionContext][getter]") {
    kth_transaction_mut_t tx = make_tx();
    kth_script_execution_context_mut_t ctx =
        kth_vm_script_execution_context_construct(0u, tx);

    // The borrowed view must expose the same version/locktime as the
    // owning transaction handle — proving the context's `transaction()`
    // accessor hands back the actual object, not a detached copy.
    kth_transaction_const_t borrowed = kth_vm_script_execution_context_transaction(ctx);
    REQUIRE(borrowed != NULL);
    REQUIRE(kth_chain_transaction_version(borrowed) == kVersion);
    REQUIRE(kth_chain_transaction_locktime(borrowed) == kLocktime);

    // Do NOT destruct `borrowed` — it's a non-owning view into the tx.
    kth_vm_script_execution_context_destruct(ctx);
    kth_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// program::context() — empty by default
// ---------------------------------------------------------------------------

TEST_CASE("C-API ScriptExecutionContext - program without ctx reports no context",
          "[C-API ScriptExecutionContext][program]") {
    // A program constructed from script alone has no execution
    // context — `context()` must return NULL. A non-NULL handle here
    // would mean the generator emitted a phantom optional.
    kth_script_mut_t script = make_script();
    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);

    kth_script_execution_context_const_t ctx = kth_vm_program_context(prog);
    REQUIRE(ctx == NULL);

    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
}

// ---------------------------------------------------------------------------
// Preconditions — const getters abort on NULL
// ---------------------------------------------------------------------------

TEST_CASE("C-API ScriptExecutionContext - input_index null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_input_index(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - input_count null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_input_count(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - output_count null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_output_count(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - tx_version null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_tx_version(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - tx_locktime null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_tx_locktime(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - transaction null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_transaction(NULL));
}

TEST_CASE("C-API ScriptExecutionContext - copy null aborts",
          "[C-API ScriptExecutionContext][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_script_execution_context_copy(NULL));
}
