// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Minimal profiling executable for comparing traversal implementations
// Run with: instruments -t "Time Profiler" ./kth_blockchain_bench_profile
// Or: time ./kth_blockchain_bench_profile <mode>
//
// Modes: soa, soa_ptr, aos, all (default)

#include "common.hpp"
#include "data_gen.hpp"
#include "03-index_based_soa.hpp"
#include "05-vector_aos_ptr_compact.hpp"

#include <fmt/core.h>
#include <string>

int main(int argc, char* argv[]) {
    constexpr size_t num_blocks = 10000;
    constexpr size_t num_iterations = 10000000;  // 10M iterations for profiling

    // Parse command line: soa, soa_ptr, aos, or all (default)
    std::string mode = "all";
    if (argc > 1) {
        mode = argv[1];
    }

    fmt::print("Generating chain with {} blocks...\n", num_blocks);
    auto chain = generate_chain(num_blocks);

    // Setup SoA store
    index_based_soa::block_index_store soa_store;
    soa_store.preallocate(num_blocks);
    for (auto const& [hash, hdr] : chain) {
        soa_store.add(hash, hdr);
    }
    uint32_t soa_tip = soa_store.find_idx(chain.back().first);

    // Setup AoS+Ptr Compact store
    vector_aos_ptr_compact::block_index_store aos_store;
    aos_store.preallocate(num_blocks);
    for (auto const& [hash, hdr] : chain) {
        aos_store.add(hash, hdr);
    }
    auto* aos_tip = aos_store.find(chain.back().first);

    fmt::print("Running {} iterations each...\n", num_iterations);
    fmt::print("Mode: {}\n\n", mode);

    int64_t total = 0;

    // SoA with index lookup
    if (mode == "all" || mode == "soa") {
        total = 0;
        fmt::print("=== SoA v1 (index lookup) ===\n");
        for (size_t i = 0; i < num_iterations; ++i) {
            int count = soa_store.traverse_back_by_index_unsafe(soa_tip, 100);
            total += count;
        }
        do_not_optimize(total);
        fmt::print("SoA v1 done. total={}\n\n", total);
    }

    // SoA with pointer chase on index array
    if (mode == "all" || mode == "soa_ptr") {
        total = 0;
        fmt::print("=== SoA v2 (pointer chase on index array) ===\n");
        for (size_t i = 0; i < num_iterations; ++i) {
            int count = soa_store.traverse_back_by_ptr_unsafe(soa_tip, 100);
            total += count;
        }
        do_not_optimize(total);
        fmt::print("SoA v2 done. total={}\n\n", total);
    }

    // AoS+Ptr Compact (72-byte struct pointer chase)
    if (mode == "all" || mode == "aos") {
        total = 0;
        fmt::print("=== AoS+Ptr Compact (72-byte pointer chase) ===\n");
        for (size_t i = 0; i < num_iterations; ++i) {
            int count = vector_aos_ptr_compact::block_index_store::traverse_back_unsafe(aos_tip, 100);
            total += count;
        }
        do_not_optimize(total);
        fmt::print("AoS+Ptr done. total={}\n\n", total);
    }

    fmt::print("Profiling complete.\n");
    return 0;
}
