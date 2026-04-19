// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Consensus probe — pairs with BCHN's
// `function_table_stack_budget_tests`. The per-op size check is:
//
//     stack.size() + altstack.size() + functionTable.size() > MAX_STACK_SIZE
//
// A static-analysis pass has flagged `functionTable.size()` as
// possibly spurious — "function definitions are not pushed to the
// stack". The BCHN sibling test demonstrates empirically that
// BCHN does enforce this term (999 DEFINEs + 2 pushes fails with
// STACK_SIZE, 999 DEFINEs + 1 push succeeds). Removing it in kth
// would be a consensus split, so this test pins kth to the same
// behaviour.
//
// We run through `interpreter::run` with a dummy transaction and no
// attached `script_execution_context`, which leaves the May 2025
// op-cost/hash-iters budgets un-armed — that is deliberate, since
// we want to isolate the MAX_STACK_SIZE check without tripping
// op_cost first (the BCHN-side test drives it via a large scriptSig
// for the same reason).

#include <test_helpers.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/error.hpp>

#include <cstdint>
#include <vector>

using namespace kth;
using namespace kd;
using namespace kth::domain::machine;

namespace {

chain::transaction const& dummy_tx() {
    static chain::transaction const tx;
    return tx;
}

// Build a script that defines `n` distinct empty-bodied functions.
// Each iteration's peak stack is 2 (body + id); OP_DEFINE pops both
// and grows function_table by 1.
chain::script make_n_defines(int n) {
    operation::list ops;
    ops.reserve(static_cast<size_t>(n) * 3);
    for (int i = 0; i < n; ++i) {
        data_chunk const id{
            static_cast<uint8_t>(i & 0xff),
            static_cast<uint8_t>((i >> 8) & 0xff),
        };
        ops.emplace_back(data_chunk{});  // empty body
        ops.emplace_back(id);
        ops.emplace_back(opcode::op_define);
    }
    return chain::script(std::move(ops));
}

// bch_vm_limits (May 2025) replaces the legacy 201-counted-ops cap
// with the op_cost mechanism; that cap would otherwise trip long
// before we drove the function_table to its interesting size.
// bch_subroutines (May 2026) activates OP_DEFINE/OP_INVOKE.
// op_cost itself only engages when a `script_execution_context` is
// attached to the program; our dummy `program` has none, so we hit
// the raw MAX_STACK_SIZE branch in isolation — intentional.
constexpr script_flags_t kFlags =
    script_flags::bch_vm_limits | script_flags::bch_subroutines;

} // namespace

// After 999 OP_DEFINEs the function_table has 999 entries, stack=0.
// Pushing one more element leaves 1+0+999 = 1000, which is NOT > 1000
// — the run must succeed.
TEST_CASE("function table + 1 push stays within MAX_STACK_SIZE",
          "[interpreter][function-table-budget][consensus]") {
    auto scr = make_n_defines(999);
    {
        // Append a final truthy push so `stack_true` has something to
        // read (we don't assert on the final value, just on run()).
        auto ops = scr.operations();
        ops.emplace_back(opcode::push_positive_1);
        scr = chain::script(std::move(ops));
    }

    program prog(scr, dummy_tx(), 0, kFlags, 0);
    auto const result = interpreter::run(prog);
    REQUIRE(bool(result));
}

// The probe: after 999 OP_DEFINEs, the *second* extra push takes the
// budget to 2+0+999 = 1001 > MAX_STACK_SIZE. If kth matches BCHN, the
// run fails with `invalid_stack_size` (kth's spelling of STACK_SIZE).
TEST_CASE("function table counts against MAX_STACK_SIZE (matches BCHN)",
          "[interpreter][function-table-budget][consensus]") {
    auto scr = make_n_defines(999);
    {
        auto ops = scr.operations();
        ops.emplace_back(opcode::push_positive_1);
        ops.emplace_back(opcode::push_positive_1);
        scr = chain::script(std::move(ops));
    }

    program prog(scr, dummy_tx(), 0, kFlags, 0);
    auto const result = interpreter::run(prog);
    REQUIRE_FALSE(bool(result));
    REQUIRE(result.error == error::invalid_stack_size);
}
