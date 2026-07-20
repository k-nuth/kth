// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Micro-benchmarks for `to_data(...)` across the chain types. On this
// branch the serialization path is the new `byte_writer` API: every
// write is bounds-checked against a caller-owned `data_chunk` sized to
// the exact `serialized_size(...)`. The helper `kth::to_data_chunk(x,
// args...)` (in `<kth/infrastructure/utility/byte_writer.hpp>`) folds
// the allocate/write/return dance into one call so the callsite reads
// like the old convenience overload.
//
// The parent PR #387 has the same benchmark file measuring the legacy
// `data_sink` + `ostream_writer` stack; the two runs share fixtures
// and are directly comparable.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <fmt/core.h>

#include <random>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

using namespace kth;
using namespace kth::domain::chain;
using ankerl::nanobench::Bench;

// -----------------------------------------------------------------------------
// Fixture builders. Deterministic seeds so the numbers reproduce across
// runs and both branches use the exact same inputs.
// -----------------------------------------------------------------------------

hash_digest fixed_hash(uint32_t salt) {
    hash_digest h{};
    std::mt19937 rng(salt);
    for (auto& b : h) b = static_cast<uint8_t>(rng());
    return h;
}

data_chunk fixed_bytes(size_t n, uint32_t salt) {
    data_chunk out(n);
    std::mt19937 rng(salt);
    for (auto& b : out) b = static_cast<uint8_t>(rng());
    return out;
}

point make_point(uint32_t salt) {
    return point{fixed_hash(salt), salt & 0xffff};
}

script make_script(size_t body_bytes, uint32_t salt) {
    // The scripts in the wild are dominated by short p2pkh (~25 B) and
    // p2sh (~23 B) payloads; benchmark uses those sizes plus a larger
    // "witness-y" 100 B script to exercise the write_bytes path.
    return script{fixed_bytes(body_bytes, salt), /*prefix=*/false};
}

input make_input(uint32_t salt) {
    return input{
        output_point{fixed_hash(salt ^ 0xa5a5), salt & 0xffff},
        make_script(25, salt ^ 0x5a5a),
        salt,
    };
}

output make_output(uint32_t salt) {
    return output{
        uint64_t(salt) * 100'000,
        make_script(25, salt ^ 0x3c3c),
        std::nullopt,   // no token data — most common on wire
    };
}

transaction make_transaction(size_t inputs, size_t outputs, uint32_t salt) {
    input::list ins;
    ins.reserve(inputs);
    for (size_t i = 0; i < inputs; ++i) {
        ins.push_back(make_input(salt + uint32_t(i)));
    }
    output::list outs;
    outs.reserve(outputs);
    for (size_t i = 0; i < outputs; ++i) {
        outs.push_back(make_output(salt * 3 + uint32_t(i)));
    }
    return transaction{
        /*version=*/2u,
        /*locktime=*/0u,
        std::move(ins),
        std::move(outs),
    };
}

header make_header(uint32_t salt) {
    return header{
        1u,
        fixed_hash(salt),
        fixed_hash(salt ^ 0xdeadbeef),
        1'700'000'000u + salt,
        0x1d00ffffu,
        salt * 7u,
    };
}

block make_block(size_t tx_count, uint32_t salt) {
    transaction::list txs;
    txs.reserve(tx_count);
    // Coinbase-ish first: 1 input, 1 output. Rest: modest 2-in / 2-out
    // so the block hits the varint path without becoming pathologically
    // large.
    txs.push_back(make_transaction(1, 1, salt));
    for (size_t i = 1; i < tx_count; ++i) {
        txs.push_back(make_transaction(2, 2, salt + uint32_t(i * 13)));
    }
    return block{make_header(salt), std::move(txs)};
}

// -----------------------------------------------------------------------------
// `byte_writer`-backed `to_data(...)` calls via the `to_data_chunk` helper.
// The helper allocates the buffer sized to `serialized_size(args...)`,
// constructs a `byte_writer` over it, and returns the filled `data_chunk`
// in one call. `doNotOptimizeAway` on the result keeps the compiler from
// folding the whole call away.
// -----------------------------------------------------------------------------

void bench_point(Bench& b) {
    auto const p = make_point(1);
    b.run("point::to_data(wire)", [&] {
        auto data = kth::to_data_chunk(p, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
    b.run("point::to_data(!wire)", [&] {
        auto data = kth::to_data_chunk(p, false);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void bench_script(Bench& b) {
    auto const s_small = make_script(25, 2);
    auto const s_large = make_script(400, 3);
    b.run("script::to_data(prefix) [25B]", [&] {
        auto data = kth::to_data_chunk(s_small, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
    b.run("script::to_data(prefix) [400B]", [&] {
        auto data = kth::to_data_chunk(s_large, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void bench_input(Bench& b) {
    auto const in = make_input(4);
    b.run("input::to_data(wire)", [&] {
        auto data = kth::to_data_chunk(in, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void bench_output(Bench& b) {
    auto const out = make_output(5);
    b.run("output::to_data(wire)", [&] {
        auto data = kth::to_data_chunk(out, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void bench_transaction(Bench& b) {
    auto const tx_small = make_transaction(1, 2, 6);
    auto const tx_big = make_transaction(20, 20, 7);
    b.run("transaction::to_data(wire) [1in/2out]", [&] {
        auto data = kth::to_data_chunk(tx_small, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
    b.run("transaction::to_data(wire) [20in/20out]", [&] {
        auto data = kth::to_data_chunk(tx_big, true);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void bench_block(Bench& b) {
    auto const blk_small = make_block(10, 8);
    auto const blk_large = make_block(500, 9);
    b.run("block::to_data() [10 tx]", [&] {
        auto data = kth::to_data_chunk(blk_small);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
    b.run("block::to_data() [500 tx]", [&] {
        auto data = kth::to_data_chunk(blk_large);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

// -----------------------------------------------------------------------------
// Entry point.
// -----------------------------------------------------------------------------

int main() {
    Bench bench;
    bench.title("to_data (byte_writer path via kth::to_data_chunk)")
         .unit("op")
         .minEpochIterations(50);

    bench_point(bench);
    bench_script(bench);
    bench_input(bench);
    bench_output(bench);
    bench_transaction(bench);
    bench_block(bench);

    fmt::print("\nDone.\n");
    return 0;
}
