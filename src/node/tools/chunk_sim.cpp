// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// =============================================================================
// Chunk Coordinator Simulation
// =============================================================================
//
// Standalone simulation of the chunk download system with mixed fast/slow peers.
// Reproduces the "roller coaster" throughput pattern observed in real sync.
//
// Usage: chunk_sim [duration_secs] [fast_peers] [slow_peers] [total_blocks] [slots]
//   duration_secs: simulation duration (default: 60)
//   fast_peers:    number of fast peers (default: 10)
//   slow_peers:    number of slow peers (default: 5)
//   total_blocks:  number of blocks to simulate (default: 1000000)
//   slots:         number of slots, 0 = one per chunk (default: 0)
//
// Output: CSV to stdout with per-second stats
//   second,chunks/s,fast_blocked,slow_blocked,slots_free,slots_progress,slots_complete
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include <kth/node/sync/chunk_coordinator.hpp>
#include <kth/blockchain/header_index.hpp>

using namespace kth;
using namespace kth::node::sync;
using namespace kth::blockchain;

// =============================================================================
// Shared counters (atomic, sampled every second by reporter)
// =============================================================================
struct sim_counters {
    std::atomic<uint64_t> chunks_completed{0};
    std::atomic<uint64_t> fast_blocked{0};
    std::atomic<uint64_t> slow_blocked{0};
};

// =============================================================================
// Peer simulation
// =============================================================================
static
void simulate_peer(
    chunk_coordinator& coord,
    sim_counters& counters,
    bool is_fast,
    unsigned seed)
{
    std::mt19937 rng(seed);

    // Fast peers: 1-5ms, Slow peers: 100-500ms
    // These distributions match real-world observations
    std::uniform_int_distribution<int> fast_dist(1, 5);
    std::uniform_int_distribution<int> slow_dist(100, 500);

    while (!coord.is_complete() && !coord.is_stopped()) {
        auto chunk = coord.claim_chunk();
        if (!chunk) {
            if (is_fast) {
                counters.fast_blocked.fetch_add(1, std::memory_order_relaxed);
            } else {
                counters.slow_blocked.fetch_add(1, std::memory_order_relaxed);
            }
            // Back off briefly
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        // Simulate download time
        int ms = is_fast ? fast_dist(rng) : slow_dist(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));

        coord.chunk_completed(*chunk);
        counters.chunks_completed.fetch_add(1, std::memory_order_relaxed);
    }
}

// =============================================================================
// Main
// =============================================================================
int main(int argc, char* argv[]) {
    int duration_secs = argc > 1 ? std::atoi(argv[1]) : 60;
    int fast_peers    = argc > 2 ? std::atoi(argv[2]) : 10;
    int slow_peers    = argc > 3 ? std::atoi(argv[3]) : 5;
    uint32_t const total_blocks = argc > 4 ? uint32_t(std::atoi(argv[4])) : 1'000'000;
    size_t const slots = argc > 5 ? size_t(std::atoi(argv[5])) : 0;

    uint32_t const chunk_size = 16;
    uint32_t const total_chunks = (total_blocks + chunk_size - 1) / chunk_size;

    chunk_coordinator_config cfg{
        .chunk_size = chunk_size,
        .slots = slots,
        .stall_timeout_secs = 15
    };

    size_t effective_slots = slots > 0 ? std::min(slots, size_t(total_chunks)) : size_t(total_chunks);

    std::cerr << "=== Chunk Coordinator Simulation ===" << std::endl;
    std::cerr << "Duration:       " << duration_secs << "s" << std::endl;
    std::cerr << "Fast peers:     " << fast_peers << " (1-5ms/chunk)" << std::endl;
    std::cerr << "Slow peers:     " << slow_peers << " (100-500ms/chunk)" << std::endl;
    std::cerr << "Slots:          " << effective_slots << (slots == 0 ? " (one per chunk)" : " (limited)") << std::endl;
    std::cerr << "Chunk size:     " << chunk_size << " blocks" << std::endl;
    std::cerr << "Total blocks:   " << total_blocks << std::endl;
    std::cerr << "Total chunks:   " << total_chunks << std::endl;
    std::cerr << std::endl;

    // Create header index with dummy data
    header_index index(total_blocks + 1);
    for (uint32_t h = 1; h <= total_blocks; ++h) {
        domain::chain::header hdr;
        hash_digest hash{};
        auto const le = to_little_endian(h);
        std::copy(le.begin(), le.end(), hash.begin());
        [[maybe_unused]] auto result = index.add(hash, hdr);
    }

    chunk_coordinator coord(index, 1, total_blocks, cfg);
    sim_counters counters;

    // Spawn timeout checker
    std::atomic<bool> running{true};
    std::thread timeout_thread([&] {
        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            coord.check_timeouts();
        }
    });

    // Spawn peer threads
    std::vector<std::thread> peers;
    unsigned seed = 42;
    for (int i = 0; i < fast_peers; ++i) {
        peers.emplace_back(simulate_peer, std::ref(coord), std::ref(counters), true, seed++);
    }
    for (int i = 0; i < slow_peers; ++i) {
        peers.emplace_back(simulate_peer, std::ref(coord), std::ref(counters), false, seed++);
    }

    // Reporter: sample counters every second
    std::cout << "second,chunks_s,fast_blocked,slow_blocked,cumulative_chunks" << std::endl;

    uint64_t prev_chunks = 0;
    uint64_t prev_fast_blocked = 0;
    uint64_t prev_slow_blocked = 0;

    for (int sec = 1; duration_secs == 0 || sec <= duration_secs; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint64_t cur_chunks = counters.chunks_completed.load(std::memory_order_relaxed);
        uint64_t cur_fast_b = counters.fast_blocked.load(std::memory_order_relaxed);
        uint64_t cur_slow_b = counters.slow_blocked.load(std::memory_order_relaxed);

        uint64_t delta_chunks = cur_chunks - prev_chunks;
        uint64_t delta_fast_b = cur_fast_b - prev_fast_blocked;
        uint64_t delta_slow_b = cur_slow_b - prev_slow_blocked;

        std::cout << sec << ","
                  << delta_chunks << ","
                  << delta_fast_b << ","
                  << delta_slow_b << ","
                  << cur_chunks
                  << std::endl;

        prev_chunks = cur_chunks;
        prev_fast_blocked = cur_fast_b;
        prev_slow_blocked = cur_slow_b;

        if (coord.is_complete()) {
            std::cerr << "Simulation complete at second " << sec << std::endl;
            break;
        }
    }

    // Stop
    coord.stop();
    running.store(false, std::memory_order_relaxed);

    for (auto& t : peers) t.join();
    timeout_thread.join();

    uint64_t total_completed = counters.chunks_completed.load();
    uint64_t total_fast_b = counters.fast_blocked.load();
    uint64_t total_slow_b = counters.slow_blocked.load();

    std::cerr << std::endl;
    std::cerr << "=== Summary ===" << std::endl;
    std::cerr << "Chunks completed: " << total_completed << std::endl;
    std::cerr << "Fast blocked:     " << total_fast_b << std::endl;
    std::cerr << "Slow blocked:     " << total_slow_b << std::endl;

    return 0;
}
