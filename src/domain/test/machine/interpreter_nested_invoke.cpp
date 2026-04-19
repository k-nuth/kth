// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Consensus regression: `OP_ACTIVEBYTECODE` inside a function that
// returned from a nested `OP_INVOKE` must read the *caller's* body,
// not the outermost script.
//
// BCHN's test vectors exercise nested `OP_INVOKE` but always end
// the inner function on the invoke itself, so their suites do not
// happen to drive this specific shape. This file carries the
// minimal reproducer.

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

chain::transaction const& dummy_tx() {
    static chain::transaction const tx;
    return tx;
}

chain::script script_of(std::initializer_list<operation> ops) {
    return chain::script(operation::list{ops.begin(), ops.end()});
}

// Opcode bytes reused across the test body. Pinned to the enum via
// `static_assert` so a silent numeric drift fails to compile
// instead of silently exercising the wrong bytecode.
constexpr uint8_t OP_ACTIVEBYTECODE_B = 0xc1;   // active_bytecode
constexpr uint8_t OP_0_B               = 0x00;  // push empty
constexpr uint8_t OP_INVOKE_B          = 0x8a;  // op_invoke

static_assert(static_cast<uint8_t>(opcode::active_bytecode) == OP_ACTIVEBYTECODE_B);
static_assert(static_cast<uint8_t>(opcode::push_size_0)     == OP_0_B);
static_assert(static_cast<uint8_t>(opcode::op_invoke)       == OP_INVOKE_B);

// FN0 body: a single OP_ACTIVEBYTECODE.
data_chunk fn0_body()  { return {OP_ACTIVEBYTECODE_B}; }

// FN1 body: `<0> OP_INVOKE OP_ACTIVEBYTECODE`. Calls FN0, then reads
// its own active bytecode — the second read is where the bug shows.
data_chunk fn1_body()  { return {OP_0_B, OP_INVOKE_B, OP_ACTIVEBYTECODE_B}; }

// Flags that activate OP_DEFINE / OP_INVOKE / OP_ACTIVEBYTECODE.
constexpr script_flags_t kTestFlags =
    script_flags::bch_subroutines | script_flags::bch_native_introspection;

} // namespace

TEST_CASE("nested OP_INVOKE: OP_ACTIVEBYTECODE after inner return reads caller's body",
          "[interpreter][nested-invoke][consensus]") {
    // Main script:
    //   <FN0>  <0>  OP_DEFINE           // define function 0 = OP_ACTIVEBYTECODE
    //   <FN1>  <1>  OP_DEFINE           // define function 1 = <0> OP_INVOKE OP_ACTIVEBYTECODE
    //   <1>    OP_INVOKE                // invoke FN1; FN1 invokes FN0, then reads bytecode
    //   <FN1>  OP_EQUALVERIFY           // top must be FN1's body (the post-inner-return read)
    //   <FN0>  OP_EQUAL                 // next must be FN0's body (pushed by inner call)
    auto scr = script_of({
        operation(fn0_body()),
        operation(opcode::push_size_0),
        operation(opcode::op_define),

        operation(fn1_body()),
        operation(opcode::push_positive_1),
        operation(opcode::op_define),

        operation(opcode::push_positive_1),
        operation(opcode::op_invoke),

        operation(fn1_body()),
        operation(opcode::equalverify),
        operation(fn0_body()),
        operation(opcode::equal),
    });

    program prog(scr, dummy_tx(), 0, kTestFlags, 0);
    auto const result = interpreter::run(prog);

    // Expected once the bug is fixed: run() succeeds AND the final
    // stack is truthy (the EQUAL left a `1` on top).
    REQUIRE(bool(result));
    REQUIRE(prog.stack_true(false));
}
