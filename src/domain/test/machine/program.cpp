// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::machine;

namespace {

chain::script script_of(std::initializer_list<operation> ops) {
    return chain::script(operation::list{ops.begin(), ops.end()});
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / observers
// ---------------------------------------------------------------------------

TEST_CASE("default program has an empty script and zero flags",
          "[program]") {
    program prog;
    REQUIRE(prog.flags() == 0);
    REQUIRE(prog.input_index() == 0);
    REQUIRE(prog.size() == 0);                  // empty primary stack
    REQUIRE(prog.empty());
}

TEST_CASE("program from script exposes begin/end iterators",
          "[program]") {
    auto scr = script_of({
        operation(opcode::push_positive_1),
        operation(opcode::push_positive_2),
    });
    program prog(scr);
    REQUIRE(std::distance(prog.begin(), prog.end()) == 2);
}

TEST_CASE("program captures the flags and input_index it was built with",
          "[program]") {
    auto scr = script_of({operation(opcode::push_positive_1)});
    chain::transaction const tx;
    program prog(scr, tx, /*input_index=*/7, /*flags=*/0x42, /*value=*/9000);
    REQUIRE(prog.input_index() == 7);
    REQUIRE(prog.flags() == 0x42);
    REQUIRE(prog.value() == 9000);
}

// ---------------------------------------------------------------------------
// Stack primitives — the interpreter leans on these heavily
// ---------------------------------------------------------------------------

TEST_CASE("push / pop roundtrip", "[program][stack]") {
    program prog;
    prog.push(true);
    REQUIRE(prog.size() == 1);
    REQUIRE_FALSE(prog.empty());
    auto const value = prog.pop();
    REQUIRE(prog.empty());
    // `push(true)` lowers to the canonical "1" encoding.
    REQUIRE(value.size() == 1);
    REQUIRE(value[0] == 0x01);
}

TEST_CASE("drop removes the top element", "[program][stack]") {
    program prog;
    prog.push(true);
    prog.push(false);
    REQUIRE(prog.size() == 2);
    prog.drop();
    REQUIRE(prog.size() == 1);
}

TEST_CASE("alternate stack push / pop", "[program][stack]") {
    program prog;
    REQUIRE(prog.empty_alternate());
    prog.push_alternate(data_stack::value_type{0x2a});
    REQUIRE_FALSE(prog.empty_alternate());
    auto const back = prog.pop_alternate();
    REQUIRE(back.size() == 1);
    REQUIRE(back[0] == 0x2a);
    REQUIRE(prog.empty_alternate());
}

// ---------------------------------------------------------------------------
// Conditional stack (IF/ELSE/ENDIF scaffolding used by debug)
// ---------------------------------------------------------------------------

TEST_CASE("conditional stack open / close", "[program][conditional]") {
    program prog;
    REQUIRE(prog.conditional_stack_size() == 0);
    REQUIRE(prog.closed());

    prog.open(true);
    REQUIRE(prog.conditional_stack_size() == 1);
    REQUIRE_FALSE(prog.closed());

    prog.close();
    REQUIRE(prog.conditional_stack_size() == 0);
    REQUIRE(prog.closed());
}

// ---------------------------------------------------------------------------
// evaluate — thin shim over interpreter::run
// ---------------------------------------------------------------------------

TEST_CASE("program::evaluate runs a valid script", "[program][evaluate]") {
    auto scr = script_of({operation(opcode::push_positive_1)});
    program prog(scr);
    auto const result = prog.evaluate();
    REQUIRE(bool(result));
}
