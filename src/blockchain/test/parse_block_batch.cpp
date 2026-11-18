// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/blockchain/validate/fork_join.hpp>
#include <kth/domain/chain/block.hpp>

using namespace kth;
using namespace kth::blockchain;
namespace chain = kth::domain::chain;

namespace {

// A block of `count` transactions after a coinbase: enough of them that a
// worker parses something, and different per block so no two blocks agree.
data_chunk raw_block(uint32_t salt, size_t count) {
    chain::transaction::list txs;
    txs.emplace_back(1, 0,
        chain::input::list{chain::input(chain::output_point(null_hash, chain::point::null_index),
            chain::script(data_chunk{0x02, uint8_t(salt), uint8_t(salt >> 8)}, false), 0xffffffff)},
        chain::output::list{chain::output(50, chain::script(data_chunk{0x51}, false), std::nullopt)});
    for (size_t t = 0; t < count; ++t) {
        hash_digest prev{};
        prev[0] = uint8_t(salt);
        prev[1] = uint8_t(t);
        txs.emplace_back(1, 0,
            chain::input::list{chain::input(chain::output_point(prev, uint32_t(t)), chain::script{}, 0)},
            chain::output::list{chain::output(t + 1, chain::script(data_chunk{0x52}, false), std::nullopt)});
    }
    chain::block const block(chain::header{1u, null_hash, null_hash, salt, 0u, 0u}, std::move(txs));
    return kth::to_data_chunk(block);
}

std::vector<data_chunk> raw_batch(size_t blocks) {
    std::vector<data_chunk> out;
    for (size_t i = 0; i < blocks; ++i) {
        out.push_back(raw_block(uint32_t(i + 1), 3 + i % 4));
    }
    return out;
}

} // namespace

TEST_CASE("parse_block_batch gives the blocks and identifiers a parse in order gives",
          "[parse_block_batch]") {
    // More blocks than threads on any machine running this, so every worker
    // takes several and the strides interleave.
    auto const raw = raw_batch(fork_join_workers(1'000) * 3 + 1);
    auto const parsed = parse_block_batch(raw);
    REQUIRE(parsed);
    REQUIRE(parsed->blocks.size() == raw.size());
    REQUIRE(parsed->txids.size() == raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        byte_reader reader(byte_span{raw[i].data(), raw[i].size()});
        auto const expected = domain::message::block::from_data(reader, 0u);
        REQUIRE(expected);
        REQUIRE(parsed->blocks[i]);
        CHECK(*parsed->blocks[i] == *expected);
        CHECK(parsed->txids[i] == expected->to_hashes());
    }
}

TEST_CASE("parse_block_batch names the lowest block that does not parse",
          "[parse_block_batch]") {
    auto raw = raw_batch(fork_join_workers(1'000) * 2 + 3);
    auto const truncate = [&](size_t i) {
        raw[i].resize(raw[i].size() / 2);
    };
    // Two damaged blocks: whichever thread reaches its block first, the
    // answer is the lower one, the block a parse in order stops at.
    truncate(raw.size() - 2);
    truncate(3);
    auto const parsed = parse_block_batch(raw);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error().block.has_value());
    CHECK(*parsed.error().block == 3u);

    // Damage below it moves the answer down.
    truncate(1);
    auto const lower = parse_block_batch(raw);
    REQUIRE_FALSE(lower);
    REQUIRE(lower.error().block.has_value());
    CHECK(*lower.error().block == 1u);
}

TEST_CASE("parse_block_batch of no blocks is an empty batch", "[parse_block_batch]") {
    auto const parsed = parse_block_batch(std::vector<data_chunk>{});
    REQUIRE(parsed);
    CHECK(parsed->blocks.empty());
    CHECK(parsed->txids.empty());
}

TEST_CASE("fork_join_for calls every index once and joins before returning", "[parse_block_batch]") {
    size_t const count = fork_join_workers(1'000) * 5 + 2;
    std::vector<int> calls(count, 0);
    REQUIRE(fork_join_for(count, [&](size_t i) { ++calls[i]; }));
    for (auto const c : calls) {
        CHECK(c == 1);
    }

    // A call that throws is reported, not propagated, and the others run.
    std::vector<int> around(count, 0);
    CHECK_FALSE(fork_join_for(count, [&](size_t i) {
        if (i == 1) {
            throw std::runtime_error("one call failed");
        }
        ++around[i];
    }));
    for (size_t i = 0; i < count; ++i) {
        CHECK(around[i] == (i == 1 ? 0 : 1));
    }
}

namespace {

// Counts the threads that ran a call since the count was last started. Thread
// ids cannot tell, since the id of a joined thread is given to the next one.
// The scheduler's threads live as long as the process, so a mark that was only
// ever set would leave them out of every count after the first: each thread
// remembers which count it was last part of. The mark is a plain number and
// not an object with a constructor: some compilers build every such
// thread-local of a file as soon as a thread touches any one of them, which
// would count threads that ran nothing.
std::atomic<size_t> threads_seen{0};
std::atomic<size_t> counting{0};
thread_local size_t counted_in = 0;

void start_counting_threads() {
    counting.fetch_add(1, std::memory_order_relaxed);
    threads_seen.store(0, std::memory_order_relaxed);
}

void count_this_thread() {
    auto const now = counting.load(std::memory_order_relaxed);
    if (counted_in != now) {
        counted_in = now;
        threads_seen.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

TEST_CASE("fork_join_for runs on the same threads call after call", "[parse_block_batch]") {
    size_t const workers = fork_join_workers(1'000);
    size_t const count = workers * 4;
    size_t const calls = 50;
    start_counting_threads();
    for (size_t call = 0; call < calls; ++call) {
        REQUIRE(fork_join_for(count, [](size_t) {
            count_this_thread();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }));
    }
    // The calling thread and the workers, whatever the number of calls. A thread
    // started and joined by every call would make this workers - 1 per call.
    CHECK(threads_seen.load() <= workers);
}

#if ! defined(KTH_BROWSER_NODE)
TEST_CASE("fork_join_for serves every calling thread from the same threads", "[parse_block_batch]") {
    // Several threads fork at once, as the stages of the node do. Each is
    // served by the threads the process shares: a caller that got threads of
    // its own would make this a multiple of the workers.
    size_t const workers = fork_join_workers(1'000);
    size_t const callers = 4;
    start_counting_threads();
    std::vector<std::vector<int>> calls(callers, std::vector<int>(workers * 4, 0));
    std::vector<char> finished(callers, 0);
    {
        std::vector<std::jthread> threads;
        for (size_t c = 0; c < callers; ++c) {
            threads.emplace_back([&, c] {
                for (int round = 0; round < 10; ++round) {
                    finished[c] = fork_join_for(calls[c].size(), [&, c](size_t i) {
                        count_this_thread();
                        ++calls[c][i];
                        std::this_thread::sleep_for(std::chrono::microseconds(200));
                    }) ? 1 : 0;
                }
            });
        }
    }
    for (size_t c = 0; c < callers; ++c) {
        CHECK(finished[c] == 1);
        for (auto const n : calls[c]) {
            CHECK(n == 10);
        }
    }
    if (workers > 1) {
        CHECK(threads_seen.load() <= workers);
    } else {
        // One hardware thread: nothing is shared out, and each caller runs its
        // own calls.
        CHECK(threads_seen.load() <= callers);
    }
}
#endif

TEST_CASE("fork_join_for finishes when the calls themselves fork", "[parse_block_batch]") {
    // Every outer call forks again, so the workers are all taken while the
    // inner calls ask for them: the caller of each inner call does the work
    // itself rather than wait for a worker that is waiting too.
    size_t const outer = fork_join_workers(1'000) * 2;
    size_t const inner = 64;
    std::vector<std::vector<int>> calls(outer, std::vector<int>(inner, 0));
    // Read on this thread after the join: the assertions are not to be made
    // from the threads of the group.
    std::atomic<bool> inner_finished{true};
    REQUIRE(fork_join_for(outer, [&](size_t o) {
        if ( ! fork_join_for(inner, [&, o](size_t i) { ++calls[o][i]; })) {
            inner_finished.store(false, std::memory_order_relaxed);
        }
    }));
    CHECK(inner_finished.load());
    for (auto const& row : calls) {
        for (auto const c : row) {
            CHECK(c == 1);
        }
    }
}

