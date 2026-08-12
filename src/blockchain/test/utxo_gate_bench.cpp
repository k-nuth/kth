// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include <kth/blockchain/utxo_gate.hpp>

using namespace kth;
using namespace kth::blockchain;

// =============================================================================
// A baseline for the gate, so a later change has something to beat (#649)
// =============================================================================
//
// Deliberately small: it measures the gate and nothing else — no store, no
// blocks — because the question a later change has to answer is what the
// exclusion itself costs, not what a batch costs.
//
// HIDDEN, via the leading [.] tag: these were running in the default suite,
// where the contended case spends real wall-clock spawning threads and racing a
// writer to measure something no correctness run needs — and a timing case on a
// shared CI machine is the classic source of a red build that means nothing.
// Run them on demand with `[bench]`.
//
// Correctness first, and this is not a threshold. It fails only if the gate
// stops making progress, so nothing here becomes a performance gate. The numbers are printed to be read by a
// person comparing implementations, which is what the follow-up issue is for:
// this mutex/condition_variable, a reader fast path on an atomic, std::shared_mutex,
// and generations inside UTXO-Z if measurement ever justifies that much.

namespace {

template <typename F>
double per_op_ns(size_t ops, F&& body) {
    auto const start = std::chrono::steady_clock::now();
    std::forward<F>(body)();
    auto const elapsed = std::chrono::steady_clock::now() - start;
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
           double(ops);
}

} // namespace

TEST_CASE("gate baseline: uncontended lease and window", "[.][utxo][gate][bench]") {
    utxo_gate gate;
    constexpr size_t ops = 200000;

    auto const read_ns = per_op_ns(ops, [&] {
        for (size_t i = 0; i < ops; ++i) {
            auto lease = gate.read();
        }
    });

    auto const write_ns = per_op_ns(ops, [&] {
        for (size_t i = 0; i < ops; ++i) {
            auto window = gate.write();
        }
    });

    std::printf("[gate-bench] uncontended: read %.0f ns/op, window %.0f ns/op\n",
        read_ns, write_ns);

    // Progress, not a threshold: a gate that stopped admitting anyone would not
    // reach here at all, and a number regressing is for a person to read.
    CHECK(read_ns >= 0.0);
    CHECK(write_ns >= 0.0);
}

TEST_CASE("gate baseline: readers under contention", "[.][utxo][gate][bench]") {
    // What the node actually does: several validators probing while a batch
    // occasionally takes the window. The interesting figure is what a reader
    // pays when it is not alone.
    utxo_gate gate;
    constexpr int threads = 8;
    constexpr size_t per_thread = 4000;

    std::atomic<bool> stop{false};
    std::atomic<size_t> windows{0};

    std::thread writer([&] {
        while ( ! stop.load()) {
            {
                // The window is HELD only for the mutation, and released before
                // the wait. Sleeping inside it measures the benchmark holding the
                // door shut rather than what the gate costs — the first version
                // of this loop did exactly that and reported readers paying
                // hundreds of microseconds they never pay in production.
                auto window = gate.write();
                windows.fetch_add(1);
            }
            // Spaced like a batch rather than a spin loop.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    auto const elapsed_ns = per_op_ns(threads * per_thread, [&] {
        std::vector<std::thread> readers;
        for (int t = 0; t < threads; ++t) {
            readers.emplace_back([&] {
                for (size_t i = 0; i < per_thread; ++i) {
                    auto lease = gate.read();
                }
            });
        }
        for (auto& r : readers) {
            r.join();
        }
    });

    stop.store(true);
    writer.join();

    std::printf("[gate-bench] %d readers + a writer: %.0f ns/read, %zu windows taken\n",
        threads, elapsed_ns, windows.load());

    // The writer really did get in, which is what makes the reader figure mean
    // something: a run where the writer starved would measure the uncontended
    // case under a different name.
    CHECK(windows.load() > 0);
    CHECK(gate.readers() == 0);
}
