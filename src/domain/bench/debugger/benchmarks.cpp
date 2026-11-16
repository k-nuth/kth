// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The script debugger against plain evaluation, on scripts of several
// lengths. A debug step should cost about one operation, whatever the length
// of the script it belongs to.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <memory>
#include <string>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>

using namespace kth;
using namespace kth::domain;
using namespace kth::domain::machine;
using ankerl::nanobench::Bench;

namespace {

// 7, then `rounds` times (dup, 2, mul, swap, sub), then 7 numequal: six
// operations a round, and one true item left.
chain::script arithmetic(size_t rounds) {
    operation::list ops{operation(opcode::push_positive_7)};
    for (size_t i = 0; i < rounds; ++i) {
        ops.emplace_back(opcode::dup);
        ops.emplace_back(opcode::push_positive_2);
        ops.emplace_back(opcode::mul);
        ops.emplace_back(opcode::swap);
        ops.emplace_back(opcode::sub);
    }
    ops.emplace_back(opcode::push_positive_7);
    ops.emplace_back(opcode::numequal);
    return chain::script(std::move(ops));
}

void measure(size_t rounds) {
    static chain::transaction const tx;
    auto const script = arithmetic(rounds);
    auto const flags = to_flags(upgrade::bch_leibniz);
    auto const steps = chain::operations(script).size();
    auto const name = [&](char const* what) { return std::string(what) + ", " + std::to_string(steps) + " operations"; };

    Bench bench;
    bench.title("script debugger").unit("operation").batch(steps).minEpochIterations(200);

    bench.run(name("evaluate"), [&] {
        program prog(script, tx, 0, flags, 0);
        ankerl::nanobench::doNotOptimizeAway(prog.evaluate());
    });

    // The snapshot is moved from step to step.
    bench.run(name("debug, moving the snapshot"), [&] {
        auto snapshot = interpreter::debug_begin(program(script, tx, 0, flags, 0));
        while ( ! snapshot.done) snapshot = interpreter::debug_step(std::move(snapshot));
        ankerl::nanobench::doNotOptimizeAway(snapshot.step);
    });

    // Every step is taken on a copy and the previous snapshot dropped: what a
    // caller that goes through the C API does.
    bench.run(name("debug, copying the snapshot"), [&] {
        auto current = std::make_unique<debug_snapshot>(interpreter::debug_begin(program(script, tx, 0, flags, 0)));
        while ( ! current->done) {
            debug_snapshot const& input = *current;
            current = std::make_unique<debug_snapshot>(interpreter::debug_step(input));
        }
        ankerl::nanobench::doNotOptimizeAway(current->step);
    });
}

} // namespace

int main() {
    for (size_t rounds : {8, 38, 160, 640}) measure(rounds);
}
