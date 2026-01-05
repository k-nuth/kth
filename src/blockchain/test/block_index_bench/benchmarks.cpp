// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <fmt/core.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>

// Block index implementations
#include "common.hpp"
#include "data_gen.hpp"
#include "00-bchn_style.hpp"
#include "01-index_based.hpp"
// #include "02-index_based_preallocated.hpp"
#include "03-index_based_soa.hpp"
// #include "04-vector_aos_ptr.hpp"
// #include "05-vector_aos_ptr_compact.hpp"
// #include "06-vector_aos_ptr_prefetch.hpp"
// #include "07-vector_aos_ptr_64.hpp"
#include "08-concurrent_node.hpp"
// #include "09-cfm_hash.hpp"
#include "10-soa_fully.hpp"
// #include "11-soa_deque.hpp"
// #include "12-soa_chunks.hpp"
#include "13-soa_chunks_lockfree.hpp"
#include "14-soa_chunks_cfm_sync.hpp"
// #include "15-tbb_cfm.hpp"
// #include "16-tbb_cfm_simple.hpp"
#include "17-soa_static.hpp"

// Knuth block_pool (original implementation)
#include <kth/blockchain/pools/block_pool.hpp>

using ankerl::nanobench::Bench;

namespace {  // Anonymous namespace - internal linkage for better optimization

// =============================================================================
// Benchmark configuration
// =============================================================================
constexpr size_t BENCH_NUM_BLOCKS = 1'000'000;
constexpr size_t BENCH_NUM_SAMPLES = 1000;  // Random starting points
constexpr uint32_t BENCH_SEED = 12345;      // Consistent randomness

// =============================================================================
// Knuth block_pool helpers
// =============================================================================

// Fixture to expose protected methods of block_pool for benchmarking
// (Same approach as test/block_pool.cpp)
class block_pool_fixture : public kth::blockchain::block_pool {
public:
    using kth::blockchain::block_pool::block_pool;

    bool exists(kth::block_const_ptr candidate_block) const {
        return kth::blockchain::block_pool::exists(candidate_block);
    }

    kth::block_const_ptr parent(kth::block_const_ptr block) const {
        return kth::blockchain::block_pool::parent(block);
    }

    void prune(size_t top_height) {
        kth::blockchain::block_pool::prune(top_height);
    }
};

// Create a minimal block with just a header (no transactions)
kth::block_const_ptr create_kth_block(kth::hash_digest const& prev_hash, uint32_t height) {
    // Create header with the given previous hash
    kth::domain::chain::header hdr;
    hdr.set_version(1);
    hdr.set_previous_block_hash(prev_hash);
    hdr.set_merkle(kth::null_hash);  // Empty merkle
    hdr.set_timestamp(1231006505 + height * 600);  // ~10 min per block
    hdr.set_bits(0x1d00ffff);
    hdr.set_nonce(height);

    // Set validation height (required by block_pool)
    hdr.validation.height = height;

    // Create block with empty transactions (just header)
    auto block = std::make_shared<kth::domain::message::block>(
        hdr,
        kth::domain::chain::transaction::list{}
    );

    return block;
}

// Generate a chain of Knuth blocks
std::vector<kth::block_const_ptr> generate_kth_chain(size_t length) {
    std::vector<kth::block_const_ptr> chain;
    chain.reserve(length);

    kth::hash_digest prev_hash = kth::null_hash;  // Genesis has null prev hash

    for (size_t i = 0; i < length; ++i) {
        auto block = create_kth_block(prev_hash, uint32_t(i));
        chain.push_back(block);
        prev_hash = block->hash();  // Next block points to this one
    }

    return chain;
}

// =============================================================================
// Benchmarks
// =============================================================================

void benchmark_sequential_insert() {
    fmt::print("\n========== SEQUENTIAL INSERT (simulating IBD) ==========\n");
    fmt::print("Measuring per-block insert time (store constructed outside benchmark)\n\n");

    constexpr size_t num_blocks = 2000000;  // 2M blocks
    auto chain = generate_chain(num_blocks);

    // BCHN-style
    {
        bchn_style::block_index_store store;
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("BCHN", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }

    // Index-based (with reserve)
    {
        index_based::block_index_store store;
        store.reserve(num_blocks);
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("Index-based", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }

    // Concurrent node map
    {
        concurrent_node::block_index_store store;
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("ConcurrentNode", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }

    // SoA Chunks LF
    {
        soa_chunks_lockfree::block_index_store store;
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("SoA Chunks LF", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }

    // SoA Chunks CFM
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(num_blocks);
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("SoA Chunks CFM", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }

    // SoA Static
    {
        soa_static::block_index_store store(num_blocks);  // Runtime capacity
        size_t idx = 0;
        Bench().title("Sequential Insert (2M)").minEpochIterations(2000000).warmup(10000)
            .run("SoA Static", [&] {
                auto const& [hash, hdr] = chain[idx++ % num_blocks];
                store.add(hash, hdr);
                ankerl::nanobench::doNotOptimizeAway(store.size());
            });
    }
}

void benchmark_lookup() {
    fmt::print("\n========== LOOKUP BY HASH ==========\n");
    fmt::print("Random hash lookup in a chain of {} blocks\n", BENCH_NUM_BLOCKS);
    fmt::print("{} random lookup queries\n\n", BENCH_NUM_SAMPLES);

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;

    // Prepare random lookup order (shuffled indices)
    std::vector<size_t> shuffle_indices(BENCH_NUM_BLOCKS);
    std::iota(shuffle_indices.begin(), shuffle_indices.end(), 0);
    std::mt19937 rng(BENCH_SEED);
    std::shuffle(shuffle_indices.begin(), shuffle_indices.end(), rng);

    // Extract hashes for lookup
    std::vector<hash_digest> hashes;
    hashes.reserve(BENCH_NUM_BLOCKS);
    for (auto const& [hash, _] : chain) {
        hashes.push_back(hash);
    }

    // BCHN-style (uses shared_lock internally in find())
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("BCHN", [&] {
                auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result);
            });
    }

    // AoS+Ptr Compact
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result);
    //         });
    // }

    // AoS+Ptr Full
    // {
    //     vector_aos_ptr::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Full", [&] {
    //             auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result);
    //         });
    // }

    // 64-byte
    // {
    //     vector_aos_ptr_64::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("64-byte", [&] {
    //             auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result);
    //         });
    // }

    // Prefetch
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result);
    //         });
    // }

    // SoA Hybrid
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("SoA Hybrid", [&] {
                auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result_idx);
            });
    }

    // SoA Fully
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result_idx);
            });
    }

    // SoA Chunks (lock-free reads)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result_idx);
    //         });
    // }

    // SoA Chunks Lock-Free (concurrent_flat_map)
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result_idx);
            });
    }

    // SoA Chunks CFM Sync
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result_idx);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result_idx);
            });
    }

    // TBB CFM
    // {
    //     tbb_cfm::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM", [&] {
    //             auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result_idx);
    //         });
    // }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             auto result_idx = store.find_idx(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result_idx);
    //         });
    // }

    // ConcurrentNode
    {
        concurrent_node::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t idx = 0;
        Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
            .run("ConcurrentNode", [&] {
                auto* result = store.find(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
                ankerl::nanobench::doNotOptimizeAway(result);
            });
    }

    // CFM-Hash
    // {
    //     cfm_hash::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t idx = 0;
    //     Bench().title("Random Lookup").minEpochIterations(10000000).warmup(1000)
    //         .run("CFM-Hash", [&] {
    //             bool result = store.contains(hashes[shuffle_indices[idx++ % BENCH_NUM_BLOCKS]]);
    //             ankerl::nanobench::doNotOptimizeAway(result);
    //         });
    // }
}

// =============================================================================
// REALISTIC TRAVERSAL BENCHMARKS - Based on actual BCHN usage patterns
//
// Three primitives tested:
// 1. traverse_back(n, visitor): Walk N blocks, execute visitor on each
// 2. get_ancestor_linear(height): Walk to ancestor at height
// 3. get_ancestor_skip(height):  Walk to ancestor at height, using skip pointers
// 4. find_common_ancestor(a, b): Find fork point of two branches
//
// Real use cases from BCHN:
// - GetMedianTimePast: traverse_back(11, collect_timestamp)
// - DAA: get_ancestor_linear(height - 144)
// - DAA: get_ancestor_skip(height - 144)
// - Finalization: get_ancestor_linear(height - 10)
// - Finalization: get_ancestor_skip(height - 10)
// - Reorgs: find_common_ancestor(old_tip, new_tip)
//
// Configuration:
// - 1 million blocks (realistic mainnet size)
// - Random starting points (consistent across implementations)
// =============================================================================

// Generate random starting points (heights) that are valid for the operation
std::vector<uint32_t> generate_random_start_points(size_t count, uint32_t min_height, uint32_t max_height, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(min_height, max_height);
    std::vector<uint32_t> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        points.push_back(dist(rng));
    }
    return points;
}

// -----------------------------------------------------------------------------
// BENCHMARK 1: GetMedianTimePast - traverse_back with visitor
// Walk back 11 blocks collecting timestamps
// Called on EVERY block validation - most frequent operation
// -----------------------------------------------------------------------------
void benchmark_get_median_time_past() {
    fmt::print("\n========== GetMedianTimePast (traverse_back + visitor) ==========\n");
    fmt::print("Walk back 11 blocks, collect timestamp from each\n");
    fmt::print("Chain: {} blocks, {} random starting points\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    constexpr int nMedianTimeSpan = 11;

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;
    auto const start_heights = generate_random_start_points(
        BENCH_NUM_SAMPLES, nMedianTimeSpan, BENCH_NUM_BLOCKS - 1, BENCH_SEED);

    // Prefetch version
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<vector_aos_ptr_prefetch::block_index*> start_ptrs;
    //     for (uint32_t h : start_heights) {
    //         start_ptrs.push_back(store.find(chain[h].first));
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             vector_aos_ptr_prefetch::block_index_store::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // SoA implementation
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        // Pre-compute starting indices
        std::vector<uint32_t> start_indices;
        for (uint32_t h : start_heights) {
            start_indices.push_back(h);  // In linear chain, height == index
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA", [&] {
                uint32_t idx = start_indices[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                store.traverse_back(idx, nMedianTimeSpan, [&](auto const& block) {
                    timestamps[i++] = block.hdr.timestamp;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // AoS+Ptr Compact (72 bytes)
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     // Pre-compute starting pointers
    //     std::vector<vector_aos_ptr_compact::block_index*> start_ptrs;
    //     for (uint32_t h : start_heights) {
    //         start_ptrs.push_back(store.find(chain[h].first));
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             vector_aos_ptr_compact::block_index_store::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // 64-byte version
    // {
    //     vector_aos_ptr_64::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<vector_aos_ptr_64::block_index*> start_ptrs;
    //     for (uint32_t h : start_heights) {
    //         start_ptrs.push_back(store.find(chain[h].first));
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("64-byte", [&] {
    //             auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             vector_aos_ptr_64::block_index_store::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // BCHN-style (lock → lookup → unlock → traverse)
    // {
    //     bchn_style::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     // Cache hashes for lookup (not pointers!)
    //     std::vector<hash_digest> start_hashes;
    //     for (uint32_t h : start_heights) {
    //         start_hashes.push_back(chain[h].first);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("BCHN", [&] {
    //             auto const& hash = start_hashes[sample_idx++ % BENCH_NUM_SAMPLES];
    //             bchn_style::block_index* tip;
    //             {
    //                 std::shared_lock lock(store.cs_main());
    //                 tip = store.lookup_block_index(hash);
    //             }  // Release lock - pointer is stable
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             bchn_style::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // BCHN-style unsafe (no lock, pre-cached pointers)
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<bchn_style::block_index*> start_ptrs;
        for (uint32_t h : start_heights) {
            start_ptrs.push_back(store.find(chain[h].first));
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("BCHN unsafe", [&] {
                auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                bchn_style::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
                    timestamps[i++] = block.hdr.timestamp;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // Vector AoS+Ptr (full size, ~104 bytes)
    // {
    //     vector_aos_ptr::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<vector_aos_ptr::block_index*> start_ptrs;
    //     for (uint32_t h : start_heights) {
    //         start_ptrs.push_back(store.find(chain[h].first));
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Full", [&] {
    //             auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             vector_aos_ptr::block_index_store::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }


    // Concurrent node map (lock-free)
    {
        concurrent_node::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<concurrent_node::block_index*> start_ptrs;
        for (uint32_t h : start_heights) {
            start_ptrs.push_back(store.find(chain[h].first));
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("ConcurrentNode", [&] {
                auto* tip = start_ptrs[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                concurrent_node::block_index_store::traverse_back(tip, nMedianTimeSpan, [&](auto const& block) {
                    timestamps[i++] = block.hdr.timestamp;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // SoA Fully (every field in its own vector)
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
                    timestamps[i++] = ts;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // SoA Chunks (lock-free traversal)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
    //                 timestamps[i++] = ts;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // SoA Chunks Lock-Free
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
                    timestamps[i++] = ts;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // SoA Chunks CFM Sync (uses CFM's bucket locking for synchronization)
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
                    timestamps[i++] = ts;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        size_t sample_idx = 0;
        Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t timestamps[nMedianTimeSpan];
                int i = 0;
                store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
                    timestamps[i++] = ts;
                });
                ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
            });
    }

    // TBB CFM
    // {
    //     tbb_cfm::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM", [&] {
    //             uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
    //                 timestamps[i++] = ts;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             uint32_t idx = start_heights[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             store.traverse_back(idx, nMedianTimeSpan, [&](uint32_t ts) {
    //                 timestamps[i++] = ts;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }

    // // CFM-Hash (hash-based traversal, lock-free)
    // {
    //     cfm_hash::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     // Store hashes instead of pointers (CFM uses hash-based traversal)
    //     std::vector<hash_digest> start_hashes;
    //     for (uint32_t h : start_heights) {
    //         start_hashes.push_back(chain[h].first);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("GetMedianTimePast (11 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("CFM-Hash", [&] {
    //             auto const& start_hash = start_hashes[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t timestamps[nMedianTimeSpan];
    //             int i = 0;
    //             store.traverse_back_visit(start_hash, nMedianTimeSpan, [&](auto const& block) {
    //                 timestamps[i++] = block.hdr.timestamp;
    //             });
    //             ankerl::nanobench::doNotOptimizeAway(timestamps[0]);
    //         });
    // }
}

// -----------------------------------------------------------------------------
// BENCHMARK 2: DAA - get_ancestor
// Jump directly to ancestor at height - 144
// Called on every block to calculate difficulty
// -----------------------------------------------------------------------------
void benchmark_daa_get_ancestor_skip() {
    fmt::print("\n========== DAA Skip (get_ancestor_skip to height-144) ==========\n");
    fmt::print("O(log n) with pskip - jump to block 144 blocks back\n");
    fmt::print("Chain: {} blocks, {} random starting points\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    constexpr int daa_window = 144;

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;
    auto const start_heights = generate_random_start_points(
        BENCH_NUM_SAMPLES, daa_window, BENCH_NUM_BLOCKS - 1, BENCH_SEED);

    // AoS+Ptr Compact - Skip
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_compact::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_compact::block_index_store::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style (lock → lookup → unlock → get_ancestor)
    // {
    //     bchn_style::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<hash_digest, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(chain[h].first, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("BCHN", [&] {
    //             auto const& [hash, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             bchn_style::block_index* tip;
    //             {
    //                 std::shared_lock lock(store.cs_main());
    //                 tip = store.lookup_block_index(hash);
    //             }  // Release lock - pointer is stable
    //             auto* ancestor = bchn_style::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style unsafe (no lock, pre-cached pointers)
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<bchn_style::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("BCHN unsafe", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = bchn_style::get_ancestor_skip(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // Prefetch - Skip
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_prefetch::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_prefetch::block_index_store::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // SoA Fully - Skip
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Hybrid - Skip
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Hybrid", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks - Skip (lock-free)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }

    // SoA Chunks Lock-Free - Skip
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks CFM Sync
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // TBB CFM
    // {
    //     tbb_cfm::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_skip (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }
}

// -----------------------------------------------------------------------------
// BENCHMARK 3: Finalization Skip - get_ancestor_skip(height - 10)
// Default maxreorgdepth = 10 blocks - O(log n) with pskip
// -----------------------------------------------------------------------------
void benchmark_finalization_check_skip() {
    fmt::print("\n========== Finalization Skip (get_ancestor_skip to height-10) ==========\n");
    fmt::print("O(log n) with pskip - default maxreorgdepth = 10\n");
    fmt::print("Chain: {} blocks, {} random starting points\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    constexpr int max_reorg_depth = 10;

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;
    auto const start_heights = generate_random_start_points(
        BENCH_NUM_SAMPLES, max_reorg_depth, BENCH_NUM_BLOCKS - 1, BENCH_SEED);

    // AoS+Ptr Compact - Skip
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_compact::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_compact::block_index_store::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style (lock → lookup → unlock → get_ancestor)
    // {
    //     bchn_style::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<hash_digest, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(chain[h].first, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("BCHN", [&] {
    //             auto const& [hash, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             bchn_style::block_index* tip;
    //             {
    //                 std::shared_lock lock(store.cs_main());
    //                 tip = store.lookup_block_index(hash);
    //             }  // Release lock - pointer is stable
    //             auto* ancestor = bchn_style::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style unsafe (no lock, pre-cached pointers)
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<bchn_style::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("BCHN unsafe", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = bchn_style::get_ancestor_skip(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // Prefetch - Skip
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_prefetch::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_prefetch::block_index_store::get_ancestor_skip(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // SoA Fully - Skip
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Hybrid - Skip
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Hybrid", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks - Skip (lock-free)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }

    // SoA Chunks Lock-Free - Skip
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks CFM Sync
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization skip (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_skip(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }
}

// -----------------------------------------------------------------------------
// BENCHMARK 3b: DAA Linear - get_ancestor_linear(height - 144) - O(n) baseline
// Compare against pskip-optimized version
// -----------------------------------------------------------------------------
void benchmark_daa_get_ancestor_linear() {
    fmt::print("\n========== DAA Linear (get_ancestor_linear to height-144) ==========\n");
    fmt::print("O(n) linear traversal - baseline for pskip comparison\n");
    fmt::print("Chain: {} blocks, {} random starting points\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    constexpr int daa_window = 144;

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;
    auto const start_heights = generate_random_start_points(
        BENCH_NUM_SAMPLES, daa_window, BENCH_NUM_BLOCKS - 1, BENCH_SEED);

    // SoA implementation
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // AoS+Ptr Compact - Linear
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_compact::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_compact::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // 64-byte version
    // {
    //     vector_aos_ptr_64::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_64::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("64-byte", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_64::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style (lock for lookup, release, then traverse)
    // {
    //     bchn_style::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<hash_digest, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(chain[h].first, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("BCHN", [&] {
    //             auto const& [hash, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             bchn_style::block_index* tip;
    //             {
    //                 std::shared_lock lock(store.cs_main());
    //                 tip = store.lookup_block_index(hash);
    //             }  // Release lock - pointer is stable
    //             auto* ancestor = bchn_style::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style unsafe (no lock, pre-cached pointers)
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<bchn_style::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("BCHN unsafe", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = bchn_style::get_ancestor_linear(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // Vector AoS+Ptr Full
    // {
    //     vector_aos_ptr::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Full", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // Prefetch - Linear
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_prefetch::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_prefetch::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // Concurrent node map
    {
        concurrent_node::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<concurrent_node::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("ConcurrentNode", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = concurrent_node::block_index_store::get_ancestor_linear(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // SoA Fully
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks - Linear (lock-free)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }

    // SoA Chunks Lock-Free - Linear
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks CFM Sync
    {
        soa_chunks_cfm_sync::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - daa_window);
        }

        size_t sample_idx = 0;
        Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - daa_window);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("DAA get_ancestor_linear (144 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }
}

// -----------------------------------------------------------------------------
// BENCHMARK 3c: Finalization Linear - get_ancestor_linear(height - 10) - O(n) baseline
// Compare against pskip-optimized version
// -----------------------------------------------------------------------------
void benchmark_finalization_check_linear() {
    fmt::print("\n========== Finalization Linear (get_ancestor_linear to height-10) ==========\n");
    fmt::print("O(n) linear traversal - baseline for pskip comparison\n");
    fmt::print("Chain: {} blocks, {} random starting points\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    constexpr int max_reorg_depth = 10;

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;
    auto const start_heights = generate_random_start_points(
        BENCH_NUM_SAMPLES, max_reorg_depth, BENCH_NUM_BLOCKS - 1, BENCH_SEED);

    // SoA implementation
    {
        index_based_soa::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // AoS+Ptr Compact - Linear
    // {
    //     vector_aos_ptr_compact::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_compact::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Compact", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_compact::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // 64-byte version
    // {
    //     vector_aos_ptr_64::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_64::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("64-byte", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_64::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style (lock for lookup, release, then traverse)
    // {
    //     bchn_style::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<hash_digest, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(chain[h].first, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("BCHN", [&] {
    //             auto const& [hash, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             bchn_style::block_index* tip;
    //             {
    //                 std::shared_lock lock(store.cs_main());
    //                 tip = store.lookup_block_index(hash);
    //             }  // Release lock - pointer is stable
    //             auto* ancestor = bchn_style::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // BCHN-style unsafe (no lock, pre-cached pointers)
    {
        bchn_style::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<bchn_style::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("BCHN unsafe", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = bchn_style::get_ancestor_linear(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // Vector AoS+Ptr Full
    // {
    //     vector_aos_ptr::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("AoS+Ptr Full", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // Prefetch - Linear
    // {
    //     vector_aos_ptr_prefetch::block_index_store store;
    //     store.preallocate(BENCH_NUM_BLOCKS);
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<vector_aos_ptr_prefetch::block_index*, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("Prefetch", [&] {
    //             auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             auto* ancestor = vector_aos_ptr_prefetch::block_index_store::get_ancestor_linear(tip, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor);
    //         });
    // }

    // Concurrent node map
    {
        concurrent_node::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<concurrent_node::block_index*, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(store.find(chain[h].first), int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("ConcurrentNode", [&] {
                auto const& [tip, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                auto* ancestor = concurrent_node::block_index_store::get_ancestor_linear(tip, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor);
            });
    }

    // SoA Fully
    {
        soa_fully::block_index_store store;
        store.preallocate(BENCH_NUM_BLOCKS);
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Fully", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks - Linear (lock-free)
    // {
    //     soa_chunks::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("SoA Chunks", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }

    // SoA Chunks Lock-Free - Linear
    {
        soa_chunks_lockfree::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks LF", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Chunks CFM Sync
    {
        soa_chunks_cfm_sync::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Chunks CFM", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // SoA Static (impl 17)
    {
        soa_static::block_index_store store;
        for (auto const& [hash, hdr] : chain) {
            store.add(hash, hdr);
        }

        std::vector<std::pair<uint32_t, int>> queries;
        for (uint32_t h : start_heights) {
            queries.emplace_back(h, int(h) - max_reorg_depth);
        }

        size_t sample_idx = 0;
        Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
            .run("SoA Static", [&] {
                auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
                ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
            });
    }

    // TBB CFM Simple
    // {
    //     tbb_cfm_simple::block_index_store store;
    //     for (auto const& [hash, hdr] : chain) {
    //         store.add(hash, hdr);
    //     }

    //     std::vector<std::pair<uint32_t, int>> queries;
    //     for (uint32_t h : start_heights) {
    //         queries.emplace_back(h, int(h) - max_reorg_depth);
    //     }

    //     size_t sample_idx = 0;
    //     Bench().title("Finalization linear (10 blocks)").minEpochIterations(10000000).warmup(1000)
    //         .run("TBB CFM Simple", [&] {
    //             auto const& [idx, target] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
    //             uint32_t ancestor_idx = store.get_ancestor_linear(idx, target);
    //             ankerl::nanobench::doNotOptimizeAway(ancestor_idx);
    //         });
    // }
}

// -----------------------------------------------------------------------------
// BENCHMARK 4: FindFork / LastCommonAncestor
// Find common ancestor of two branches (reorg scenario)
// Distance varies - we test with branches diverging at different depths
// -----------------------------------------------------------------------------
void benchmark_find_common_ancestor() {
    fmt::print("\n========== FindFork / LastCommonAncestor ==========\n");
    fmt::print("Find common ancestor of two branches (reorg scenario)\n");
    fmt::print("Chain: {} blocks, {} random starting points per depth\n\n", BENCH_NUM_BLOCKS, BENCH_NUM_SAMPLES);

    auto const chain_data = generate_realistic_chain(BENCH_NUM_BLOCKS, 0.001, 42);
    auto const& chain = chain_data.blocks;

    for (int fork_depth : {10, 50, 100}) {
        fmt::print("--- Fork depth: {} blocks ---\n", fork_depth);

        auto const start_heights = generate_random_start_points(
            BENCH_NUM_SAMPLES, fork_depth, BENCH_NUM_BLOCKS - 1, BENCH_SEED + fork_depth);

        // SoA implementation
        {
            index_based_soa::block_index_store store;
            store.preallocate(BENCH_NUM_BLOCKS);
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            // Pre-compute query pairs (tip_idx, fork_point_idx)
            std::vector<std::pair<uint32_t, uint32_t>> queries;
            for (uint32_t h : start_heights) {
                uint32_t tip_idx = h;
                uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
                queries.emplace_back(tip_idx, fork_idx);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("SoA (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    uint32_t common = store.find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // AoS+Ptr Compact
        // {
        //     vector_aos_ptr_compact::block_index_store store;
        //     store.preallocate(BENCH_NUM_BLOCKS);
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<vector_aos_ptr_compact::block_index*, vector_aos_ptr_compact::block_index*>> queries;
        //     for (uint32_t h : start_heights) {
        //         auto* tip = store.find(chain[h].first);
        //         auto* fork_point = vector_aos_ptr_compact::block_index_store::get_ancestor_skip(tip, int(h) - fork_depth);
        //         queries.emplace_back(tip, fork_point);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("AoS+Ptr (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             auto* common = vector_aos_ptr_compact::block_index_store::find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // BCHN-style (lock for lookup, release, then traverse)
        // {
        //     bchn_style::block_index_store store;
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     // Cache hash pairs for lookup (not pointers!)
        //     std::vector<std::pair<hash_digest, int>> query_hashes;
        //     for (uint32_t h : start_heights) {
        //         query_hashes.emplace_back(chain[h].first, int(h) - fork_depth);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("BCHN (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [hash, target_height] = query_hashes[sample_idx++ % BENCH_NUM_SAMPLES];
        //             bchn_style::block_index* tip;
        //             {
        //                 std::shared_lock lock(store.cs_main());
        //                 tip = store.lookup_block_index(hash);
        //             }  // Release lock - pointer is stable
        //             auto* fork_point = bchn_style::get_ancestor_skip(tip, target_height);
        //             auto* common = bchn_style::find_common_ancestor(tip, fork_point);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // BCHN-style unsafe (no lock, pre-cached pointers)
        {
            bchn_style::block_index_store store;
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<bchn_style::block_index*, bchn_style::block_index*>> queries;
            for (uint32_t h : start_heights) {
                auto* tip = store.find(chain[h].first);
                auto* fork_point = bchn_style::get_ancestor_skip(tip, int(h) - fork_depth);
                queries.emplace_back(tip, fork_point);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("BCHN unsafe (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    auto* common = bchn_style::find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // Vector AoS+Ptr Full
        // {
        //     vector_aos_ptr::block_index_store store;
        //     store.preallocate(BENCH_NUM_BLOCKS);
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<vector_aos_ptr::block_index*, vector_aos_ptr::block_index*>> queries;
        //     for (uint32_t h : start_heights) {
        //         auto* tip = store.find(chain[h].first);
        //         auto* fork_point = vector_aos_ptr::block_index_store::get_ancestor_linear(tip, int(h) - fork_depth);
        //         queries.emplace_back(tip, fork_point);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("AoS Full (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             auto* common = vector_aos_ptr::block_index_store::find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // Prefetch
        // {
        //     vector_aos_ptr_prefetch::block_index_store store;
        //     store.preallocate(BENCH_NUM_BLOCKS);
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<vector_aos_ptr_prefetch::block_index*, vector_aos_ptr_prefetch::block_index*>> queries;
        //     for (uint32_t h : start_heights) {
        //         auto* tip = store.find(chain[h].first);
        //         auto* fork_point = vector_aos_ptr_prefetch::block_index_store::get_ancestor_skip(tip, int(h) - fork_depth);
        //         queries.emplace_back(tip, fork_point);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("Prefetch (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             auto* common = vector_aos_ptr_prefetch::block_index_store::find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // Concurrent node map
        {
            concurrent_node::block_index_store store;
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<concurrent_node::block_index*, concurrent_node::block_index*>> queries;
            for (uint32_t h : start_heights) {
                auto* tip = store.find(chain[h].first);
                auto* fork_point = concurrent_node::block_index_store::get_ancestor_linear(tip, int(h) - fork_depth);
                queries.emplace_back(tip, fork_point);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("ConcNode (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    auto* common = concurrent_node::block_index_store::find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // SoA Fully
        {
            soa_fully::block_index_store store;
            store.preallocate(BENCH_NUM_BLOCKS);
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<uint32_t, uint32_t>> queries;
            for (uint32_t h : start_heights) {
                uint32_t tip_idx = h;
                uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
                queries.emplace_back(tip_idx, fork_idx);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("SoA Fully (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    uint32_t common = store.find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // SoA Chunks (lock-free)
        // {
        //     soa_chunks::block_index_store store;
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<uint32_t, uint32_t>> queries;
        //     for (uint32_t h : start_heights) {
        //         uint32_t tip_idx = h;
        //         uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
        //         queries.emplace_back(tip_idx, fork_idx);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("SoA Chunks (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             uint32_t common = store.find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // SoA Chunks Lock-Free
        {
            soa_chunks_lockfree::block_index_store store;
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<uint32_t, uint32_t>> queries;
            for (uint32_t h : start_heights) {
                uint32_t tip_idx = h;
                uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
                queries.emplace_back(tip_idx, fork_idx);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("SoA Chunks LF (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    uint32_t common = store.find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // SoA Chunks CFM Sync
        {
            soa_chunks_cfm_sync::block_index_store store;
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<uint32_t, uint32_t>> queries;
            for (uint32_t h : start_heights) {
                uint32_t tip_idx = h;
                uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
                queries.emplace_back(tip_idx, fork_idx);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("SoA Chunks CFM (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    uint32_t common = store.find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // SoA Static (impl 17)
        {
            soa_static::block_index_store store;
            for (auto const& [hash, hdr] : chain) {
                store.add(hash, hdr);
            }

            std::vector<std::pair<uint32_t, uint32_t>> queries;
            for (uint32_t h : start_heights) {
                uint32_t tip_idx = h;
                uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
                queries.emplace_back(tip_idx, fork_idx);
            }

            size_t sample_idx = 0;
            std::string name = fmt::format("SoA Static (depth={})", fork_depth);
            Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
                .run(name, [&] {
                    auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
                    uint32_t common = store.find_common_ancestor(a, b);
                    ankerl::nanobench::doNotOptimizeAway(common);
                });
        }

        // TBB CFM Simple
        // {
        //     tbb_cfm_simple::block_index_store store;
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<uint32_t, uint32_t>> queries;
        //     for (uint32_t h : start_heights) {
        //         uint32_t tip_idx = h;
        //         uint32_t fork_idx = store.get_ancestor_linear(tip_idx, int(h) - fork_depth);
        //         queries.emplace_back(tip_idx, fork_idx);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("TBB CFM Simple (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             uint32_t common = store.find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }

        // // CFM-Hash
        // {
        //     cfm_hash::block_index_store store;
        //     for (auto const& [hash, hdr] : chain) {
        //         store.add(hash, hdr);
        //     }

        //     std::vector<std::pair<hash_digest, hash_digest>> queries;
        //     for (uint32_t h : start_heights) {
        //         hash_digest tip_hash = chain[h].first;
        //         hash_digest fork_hash = store.get_ancestor_linear(tip_hash, int(h) - fork_depth);
        //         queries.emplace_back(tip_hash, fork_hash);
        //     }

        //     size_t sample_idx = 0;
        //     std::string name = fmt::format("CFM-Hash (depth={})", fork_depth);
        //     Bench().title("FindCommonAncestor").minEpochIterations(10000000).warmup(1000)
        //         .run(name, [&] {
        //             auto const& [a, b] = queries[sample_idx++ % BENCH_NUM_SAMPLES];
        //             hash_digest common = store.find_common_ancestor(a, b);
        //             ankerl::nanobench::doNotOptimizeAway(common);
        //         });
        // }
    }
}


void benchmark_concurrent_insert() {
    constexpr size_t num_threads = 11;  // hw.ncpu - 1
    fmt::print("\n========== CONCURRENT INSERT ({} threads) ==========\n", num_threads);
    fmt::print("NOTE: Knuth block_pool requires external mutex (like block_organizer uses)\n\n");

    constexpr size_t blocks_per_thread = 10000;

    // Generate separate chains for each thread
    std::vector<std::vector<std::pair<hash_digest, header_data>>> thread_chains;
    std::vector<std::vector<kth::block_const_ptr>> kth_thread_chains;
    for (size_t t = 0; t < num_threads; ++t) {
        thread_chains.push_back(generate_chain(blocks_per_thread, 42 + t));
        kth_thread_chains.push_back(generate_kth_chain(blocks_per_thread));
    }

    Bench().title("Concurrent Insert (11 threads x 10K blocks)").minEpochIterations(10)
        .run("BCHN-style (shared_mutex)", [&] {
            bchn_style::block_index_store store;
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("Index-based (shared_mutex)", [&] {
            index_based::block_index_store store;
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("Concurrent node map (lock-free)", [&] {
            concurrent_node::block_index_store store;
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("SoA Chunks LF (spinlock)", [&] {
            soa_chunks_lockfree::block_index_store store;
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("SoA Chunks CFM (bucket lock)", [&] {
            soa_chunks_cfm_sync::block_index_store store;
            store.preallocate(num_threads * blocks_per_thread);
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("SoA Static (bucket lock)", [&] {
            soa_static::block_index_store store(num_threads * blocks_per_thread);
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&store, &chain = thread_chains[t]] {
                    for (auto const& [hash, hdr] : chain) {
                        store.add(hash, hdr);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("Knuth block_pool (external mutex)", [&] {
            // Simulate real usage: block_organizer wraps block_pool with prioritized_mutex
            kth::blockchain::block_pool pool(10000);
            std::mutex external_mutex;  // Like block_organizer's mutex_
            std::vector<std::thread> threads;

            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&pool, &external_mutex, &chain = kth_thread_chains[t]] {
                    for (auto const& block : chain) {
                        std::lock_guard lock(external_mutex);
                        pool.add(block);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(pool.size());
        });
}

void benchmark_concurrent_read_write() {
    constexpr int num_writers = 5;
    constexpr int num_readers = 6;  // Total: 11 threads
    fmt::print("\n========== CONCURRENT READ/WRITE ({} writers + {} readers) ==========\n", num_writers, num_readers);
    fmt::print("NOTE: Knuth block_pool requires external mutex for writes (filter() is thread-safe for reads)\n\n");

    constexpr size_t num_blocks = 50000;
    auto chain = generate_chain(num_blocks);
    auto kth_chain = generate_kth_chain(num_blocks);

    // Prepare lookup hashes (first 25K blocks)
    std::vector<hash_digest> lookup_hashes;
    for (size_t i = 0; i < 25000; ++i) {
        lookup_hashes.push_back(chain[i].first);
    }

    size_t const blocks_per_writer = 25000 / num_writers;

    Bench().title("Mixed Read/Write (5 writers + 6 readers)").minEpochIterations(10)
        .run("BCHN-style", [&] {
            bchn_style::block_index_store store;
            std::atomic<bool> done{false};
            std::atomic<size_t> read_count{0};

            // Pre-insert first 25K
            for (size_t i = 0; i < 25000; ++i) {
                store.add(chain[i].first, chain[i].second);
            }

            std::vector<std::thread> threads;

            // Writers (insert remaining 25K)
            for (int w = 0; w < num_writers; ++w) {
                threads.emplace_back([&store, &chain, &done, w, blocks_per_writer] {
                    size_t start = 25000 + w * blocks_per_writer;
                    size_t end = start + blocks_per_writer;
                    for (size_t i = start; i < end; ++i) {
                        store.add(chain[i].first, chain[i].second);
                    }
                    done = true;
                });
            }

            // Readers
            for (int r = 0; r < num_readers; ++r) {
                threads.emplace_back([&store, &lookup_hashes, &done, &read_count] {
                    size_t idx = 0;
                    while (!done.load()) {
                        auto* result = store.find(lookup_hashes[idx % lookup_hashes.size()]);
                        ankerl::nanobench::doNotOptimizeAway(result);
                        ++idx;
                        ++read_count;
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        .run("Concurrent node map", [&] {
            concurrent_node::block_index_store store;
            std::atomic<bool> done{false};
            std::atomic<size_t> read_count{0};

            // Pre-insert first 25K
            for (size_t i = 0; i < 25000; ++i) {
                store.add(chain[i].first, chain[i].second);
            }

            std::vector<std::thread> threads;

            // Writers
            for (int w = 0; w < num_writers; ++w) {
                threads.emplace_back([&store, &chain, &done, w, blocks_per_writer] {
                    size_t start = 25000 + w * blocks_per_writer;
                    size_t end = start + blocks_per_writer;
                    for (size_t i = start; i < end; ++i) {
                        store.add(chain[i].first, chain[i].second);
                    }
                    done = true;
                });
            }

            // Readers
            for (int r = 0; r < num_readers; ++r) {
                threads.emplace_back([&store, &lookup_hashes, &done, &read_count] {
                    size_t idx = 0;
                    while (!done.load()) {
                        auto* result = store.find(lookup_hashes[idx % lookup_hashes.size()]);
                        ankerl::nanobench::doNotOptimizeAway(result);
                        ++idx;
                        ++read_count;
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(store.size());
        })
        // .run("CFM-Hash (lock-free)", [&] {
        //     cfm_hash::block_index_store store;
        //     std::atomic<bool> done{false};
        //     std::atomic<size_t> read_count{0};

        //     // Pre-insert first 25K
        //     for (size_t i = 0; i < 25000; ++i) {
        //         store.add(chain[i].first, chain[i].second);
        //     }

        //     std::vector<std::thread> threads;

        //     // Writers
        //     for (int w = 0; w < num_writers; ++w) {
        //         threads.emplace_back([&store, &chain, &done, w, blocks_per_writer] {
        //             size_t start = 25000 + w * blocks_per_writer;
        //             size_t end = start + blocks_per_writer;
        //             for (size_t i = start; i < end; ++i) {
        //                 store.add(chain[i].first, chain[i].second);
        //             }
        //             done = true;
        //         });
        //     }

        //     // Readers
        //     for (int r = 0; r < num_readers; ++r) {
        //         threads.emplace_back([&store, &lookup_hashes, &done, &read_count] {
        //             size_t idx = 0;
        //             while (!done.load()) {
        //                 bool result = store.contains(lookup_hashes[idx % lookup_hashes.size()]);
        //                 ankerl::nanobench::doNotOptimizeAway(result);
        //                 ++idx;
        //                 ++read_count;
        //             }
        //         });
        //     }

        //     for (auto& th : threads) {
        //         th.join();
        //     }
        //     ankerl::nanobench::doNotOptimizeAway(store.size());
        // })
        .run("Knuth block_pool (external mutex)", [&] {
            // Real usage: block_organizer uses prioritized_mutex for all operations
            // filter() is the only thread-safe read operation, but we use exists() for comparison
            block_pool_fixture pool(10000);
            std::shared_mutex external_mutex;  // Allow concurrent reads
            std::atomic<bool> done{false};
            std::atomic<size_t> read_count{0};

            // Pre-insert first 25K
            for (size_t i = 0; i < 25000; ++i) {
                pool.add(kth_chain[i]);
            }

            std::vector<std::thread> threads;

            // Writers (insert remaining 25K)
            for (int w = 0; w < num_writers; ++w) {
                threads.emplace_back([&pool, &external_mutex, &kth_chain, &done, w, blocks_per_writer] {
                    size_t start = 25000 + w * blocks_per_writer;
                    size_t end = start + blocks_per_writer;
                    for (size_t i = start; i < end; ++i) {
                        std::unique_lock lock(external_mutex);
                        pool.add(kth_chain[i]);
                    }
                    done = true;
                });
            }

            // Readers (use exists() for comparison)
            for (int r = 0; r < num_readers; ++r) {
                threads.emplace_back([&pool, &external_mutex, &kth_chain, &done, &read_count] {
                    size_t idx = 0;
                    while (!done.load()) {
                        std::shared_lock lock(external_mutex);
                        bool result = pool.exists(kth_chain[idx % 25000]);
                        ankerl::nanobench::doNotOptimizeAway(result);
                        ++idx;
                        ++read_count;
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }
            ankerl::nanobench::doNotOptimizeAway(pool.size());
        });
}

// =============================================================================
// GENERIC TSAN TEST FRAMEWORK
// Reusable functions for testing concurrent access patterns
// =============================================================================

struct tsan_test_config {
    size_t initial_blocks = 100000;
    size_t additional_blocks = 100000;
    size_t num_writers = 1;
    size_t num_readers = 8;
    int traversal_depth = 500;
    int test_duration_ms = 3000;
    char const* test_name = "Generic";
};

struct tsan_test_stats {
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};
};

// Generic TSAN test runner
// - Generates chain data
// - Pre-inserts initial blocks using safe_add
// - Caches pointers for traversal
// - Starts all threads simultaneously with start_flag
// - Writers use writer_fn, readers use reader_fn
template<typename Store, typename SafeAddFn, typename WriterFn, typename CacheFn, typename ReaderFn>
void run_tsan_test(
    tsan_test_config const& cfg,
    Store& store,
    SafeAddFn safe_add,      // (store, hash, hdr) -> void* or index - for initial blocks
    WriterFn writer_fn,      // (store, hash, hdr) -> void - called by writer threads
    CacheFn cache_fn,        // (store, hash) -> CachedType - to cache start points
    ReaderFn reader_fn       // (store, cached, depth) -> void - called by reader threads
) {
    fmt::print("\n========== {} TSAN TEST ==========\n", cfg.test_name);
    fmt::print("  Config: {} initial + {} additional blocks\n", cfg.initial_blocks, cfg.additional_blocks);
    fmt::print("  Config: {} writers, {} readers, depth {}\n", cfg.num_writers, cfg.num_readers, cfg.traversal_depth);
    fmt::print("  Config: {} ms duration\n", cfg.test_duration_ms);
    fmt::print("  All threads start simultaneously via start_flag\n\n");

    auto chain = generate_chain(cfg.initial_blocks + cfg.additional_blocks);

    tsan_test_stats stats;
    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop{false};

    // Pre-insert initial blocks (safe)
    for (size_t i = 0; i < cfg.initial_blocks; ++i) {
        safe_add(store, chain[i].first, chain[i].second);
    }

    // Cache start points for readers
    using CachedType = decltype(cache_fn(store, chain[0].first));
    std::vector<CachedType> cached_starts;
    cached_starts.reserve(cfg.initial_blocks / 100);
    for (size_t i = cfg.traversal_depth; i < cfg.initial_blocks; i += 100) {
        cached_starts.push_back(cache_fn(store, chain[i].first));
    }
    fmt::print("  Cached {} start points\n", cached_starts.size());

    std::vector<std::thread> threads;

    // Shared write index - writers grab next block to insert atomically
    // This ensures sequential insertion (each block depends on prev_block_hash)
    std::atomic<size_t> write_idx{cfg.initial_blocks};
    size_t const max_idx = cfg.initial_blocks + cfg.additional_blocks;

    // Writers - all compete to insert the next block in sequence
    for (size_t w = 0; w < cfg.num_writers; ++w) {
        threads.emplace_back([&] {
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_relaxed)) {
                size_t i = write_idx.fetch_add(1, std::memory_order_relaxed);
                if (i >= max_idx) break;
                writer_fn(store, chain[i].first, chain[i].second);
                ++stats.write_count;
            }
        });
    }

    // Readers
    for (size_t r = 0; r < cfg.num_readers; ++r) {
        threads.emplace_back([&, r] {
            std::mt19937 rng(unsigned(r));
            std::uniform_int_distribution<size_t> dist(0, cached_starts.size() - 1);
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_relaxed)) {
                auto const& start_point = cached_starts[dist(rng)];
                reader_fn(store, start_point, cfg.traversal_depth);
                ++stats.read_count;
            }
        });
    }

    // Start all threads simultaneously
    fmt::print("  Starting {} writers + {} readers...\n", cfg.num_writers, cfg.num_readers);
    start_flag.store(true, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", stats.write_count.load());
    fmt::print("  Reads:  {}\n", stats.read_count.load());
}

// =============================================================================
// BCHN-style: lock for insert+lookup, unlock for traversal
// Should be SAFE due to std::unordered_map pointer stability
// =============================================================================
void test_bchn_pointer_stability() {
    tsan_test_config cfg;
    cfg.test_name = "BCHN Pointer Stability";
    cfg.initial_blocks = 100000;
    cfg.additional_blocks = 100000;
    cfg.num_writers = 1;
    cfg.num_readers = 8;
    cfg.traversal_depth = 500;
    cfg.test_duration_ms = 3000;

    bchn_style::block_index_store store;

    run_tsan_test(
        cfg,
        store,
        // safe_add: lock for insert
        [](auto& s, auto const& hash, auto const& hdr) {
            return s.add(hash, hdr);
        },
        // writer_fn: lock for insert
        [](auto& s, auto const& hash, auto const& hdr) {
            s.add(hash, hdr);
        },
        // cache_fn: lock for lookup, return pointer
        [](auto& s, auto const& hash) -> bchn_style::block_index* {
            return s.find(hash);
        },
        // reader_fn: traverse WITHOUT lock (testing pointer stability)
        [](auto& /*s*/, bchn_style::block_index* start, int depth) {
            auto* walk = start;
            int count = 0;
            while (walk != nullptr && count < depth) {
                walk = walk->pprev;
                ++count;
            }
            ankerl::nanobench::doNotOptimizeAway(count);
        }
    );

    fmt::print("\nBCHN uses std::unordered_map - pointer stability guaranteed.\n");
    fmt::print("If NO TSan errors: lock-for-lookup + unlock-for-traversal is SAFE.\n");
}

// =============================================================================
// SoA Fully: lock for insert+lookup, unlock for traversal
// Should FAIL due to std::vector pointer instability on resize
// =============================================================================
void test_soa_pointer_instability() {
    tsan_test_config cfg;
    cfg.test_name = "SoA Pointer Instability";
    cfg.initial_blocks = 1000;       // Small to force resizes
    cfg.additional_blocks = 200000;
    cfg.num_writers = 1;
    cfg.num_readers = 8;
    cfg.traversal_depth = 100;
    cfg.test_duration_ms = 3000;

    soa_fully::block_index_store store;
    // NO preallocate - force resizes!

    run_tsan_test(
        cfg,
        store,
        // safe_add: lock for insert
        [](auto& s, auto const& hash, auto const& hdr) {
            return s.add(hash, hdr);
        },
        // writer_fn: NO lock (unsafe!) to expose the race
        [](auto& s, auto const& hash, auto const& hdr) {
            s.add_unsafe(hash, hdr);
        },
        // cache_fn: lock for lookup, return index
        [](auto& s, auto const& hash) -> uint32_t {
            return s.find_idx(hash);
        },
        // reader_fn: traverse WITHOUT lock (testing vector stability)
        [](auto& s, uint32_t start_idx, int depth) {
            uint32_t idx = start_idx;
            int count = 0;
            while (idx != soa_fully::null_index && count < depth) {
                idx = s.get_parent_idx(idx);
                ++count;
            }
            ankerl::nanobench::doNotOptimizeAway(count);
        }
    );

    fmt::print("\nSoA uses std::vector - NO pointer stability on resize!\n");
    fmt::print("TSan SHOULD report races here.\n");
}

// =============================================================================
// SoA Deque: lock for insert+lookup, unlock for traversal
// Should be SAFE due to std::deque reference stability on push_back()
// =============================================================================
// void test_soa_deque_stability() {
//     tsan_test_config cfg;
//     cfg.test_name = "SoA Deque Stability";
//     cfg.initial_blocks = 1000;       // Small to force many push_backs
//     cfg.additional_blocks = 200000;
//     cfg.num_writers = 1;
//     cfg.num_readers = 8;
//     cfg.traversal_depth = 100;
//     cfg.test_duration_ms = 3000;

//     soa_deque::block_index_store store;
//     // NO preallocate (deque doesn't have reserve for elements anyway)

//     run_tsan_test(
//         cfg,
//         store,
//         // safe_add: lock for insert
//         [](auto& s, auto const& hash, auto const& hdr) {
//             return s.add(hash, hdr);
//         },
//         // writer_fn: NO lock (unsafe!) to test reference stability
//         [](auto& s, auto const& hash, auto const& hdr) {
//             s.add_unsafe(hash, hdr);
//         },
//         // cache_fn: lock for lookup, return index
//         [](auto& s, auto const& hash) -> uint32_t {
//             return s.find_idx(hash);
//         },
//         // reader_fn: traverse WITHOUT lock (testing deque stability)
//         [](auto& s, uint32_t start_idx, int depth) {
//             uint32_t idx = start_idx;
//             int count = 0;
//             while (idx != soa_deque::null_index && count < depth) {
//                 idx = s.get_parent_idx(idx);
//                 ++count;
//             }
//             ankerl::nanobench::doNotOptimizeAway(count);
//         }
//     );

//     fmt::print("\nSoA Deque uses std::deque - reference stability on push_back().\n");
//     fmt::print("If NO TSan errors: deque is safe for lock-free traversal.\n");
// }

// =============================================================================
// SoA Chunks: lock for insert+lookup, lock-free traversal
// Should be SAFE due to:
//   - Fixed directory (never reallocates)
//   - Chunks never move once allocated
//   - Atomic size_ with acquire/release semantics
// =============================================================================
// void test_soa_chunks_lock_free() {
//     tsan_test_config cfg;
//     cfg.test_name = "SoA Chunks Lock-Free";
//     cfg.initial_blocks = 10000;
//     cfg.additional_blocks = 1'500'000;  // > 1M to cross into second chunk
//     cfg.num_writers = 1;                // More threads = more contention
//     cfg.num_readers = 8;               // Stress test for TSan
//     cfg.traversal_depth = 500;
//     cfg.test_duration_ms = 30000;       // 30 seconds to cross into chunk 1

//     fmt::print("  CHUNK_SIZE = {} (~1M)\n", soa_chunks::CHUNK_SIZE);
//     fmt::print("  Total blocks = {} (will use 2 chunks)\n\n", cfg.initial_blocks + cfg.additional_blocks);

//     soa_chunks::block_index_store store;

//     run_tsan_test(
//         cfg,
//         store,
//         // safe_add: lock for insert
//         [](auto& s, auto const& hash, auto const& hdr) {
//             return s.add(hash, hdr);
//         },
//         // writer_fn: lock for insert (chunks need proper allocation)
//         [](auto& s, auto const& hash, auto const& hdr) {
//             s.add(hash, hdr);
//         },
//         // cache_fn: lock for lookup, return index
//         [](auto& s, auto const& hash) -> uint32_t {
//             return s.find_idx(hash);
//         },
//         // reader_fn: use traverse_back (lock-free!)
//         [](auto& s, uint32_t start_idx, int depth) {
//             int count = 0;
//             s.traverse_back(start_idx, depth, [&](uint32_t /*timestamp*/) {
//                 ++count;
//             });
//             ankerl::nanobench::doNotOptimizeAway(count);
//         }
//     );

//     fmt::print("\nSoA Chunks: fixed directory + immutable chunks.\n");
//     fmt::print("Final size: {} (crossed to chunk {})\n", store.size(), store.size() / soa_chunks::CHUNK_SIZE);
//     fmt::print("If NO TSan errors: lock-free traversal is SAFE!\n");
// }

// =============================================================================
// SoA Chunks Lock-Free: CAS spinlock + concurrent_flat_map
// Should be SAFE due to:
//   - concurrent_flat_map for hash lookup (no mutex needed)
//   - CAS spinlock for writers (lighter than mutex)
//   - Fixed directory (never reallocates)
//   - Chunks never move once allocated
//   - Atomic size_ with acquire/release semantics
//   - Chunk pointer caching (only reload on boundary)
// =============================================================================
void test_soa_chunks_lockfree_concurrent() {
    tsan_test_config cfg;
    cfg.test_name = "SoA Chunks LF (concurrent_flat_map + CAS)";
    cfg.initial_blocks = 10000;
    cfg.additional_blocks = 1'500'000;  // > 1M to cross into second chunk
    cfg.num_writers = 1;                // Writers serialize via CAS spinlock
    cfg.num_readers = 8;                // Stress test for TSan
    cfg.traversal_depth = 500;
    cfg.test_duration_ms = 30000;       // 30 seconds to cross into chunk 1

    fmt::print("  CHUNK_SIZE = {} (~1M)\n", soa_chunks_lockfree::CHUNK_SIZE);
    fmt::print("  Total blocks = {} (will use 2 chunks)\n\n", cfg.initial_blocks + cfg.additional_blocks);

    soa_chunks_lockfree::block_index_store store;

    run_tsan_test(
        cfg,
        store,
        // safe_add: CAS spinlock protects insert
        [](auto& s, auto const& hash, auto const& hdr) {
            return s.add(hash, hdr);
        },
        // writer_fn: CAS spinlock protects insert
        [](auto& s, auto const& hash, auto const& hdr) {
            s.add(hash, hdr);
        },
        // cache_fn: lock-free lookup via concurrent_flat_map!
        [](auto& s, auto const& hash) -> uint32_t {
            return s.find_idx(hash);
        },
        // reader_fn: use traverse_back (completely lock-free with chunk caching!)
        [](auto& s, uint32_t start_idx, int depth) {
            int count = 0;
            s.traverse_back(start_idx, depth, [&](uint32_t /*timestamp*/) {
                ++count;
            });
            ankerl::nanobench::doNotOptimizeAway(count);
        }
    );

    fmt::print("\nSoA Chunks LF: concurrent_flat_map + CAS spinlock.\n");
    fmt::print("Final size: {} (crossed to chunk {})\n", store.size(), store.size() / soa_chunks_lockfree::CHUNK_SIZE);
    fmt::print("If NO TSan errors: fully lock-free reads are SAFE!\n");
}

// =============================================================================
// SoA Chunks CFM Sync (impl 14): Uses CFM's bucket locking for synchronization
// Key difference from impl 13: NO explicit spinlock, relies on CFM bucket lock
// + happens-before transitivity through the ancestor chain.
//
// Thread-safety analysis:
// - Writers: insert_and_visit provides bucket-level locking
// - Chunk writes happen INSIDE the callback (under bucket lock)
// - Readers: visit() synchronizes with insert_and_visit() for the same element
// - Traversal: safe via transitivity - visiting B synchronizes with B's insert,
//   which visited parent P, which synchronizes with P's insert, etc.
// =============================================================================
void test_soa_chunks_cfm_sync_concurrent() {
    tsan_test_config cfg;
    cfg.test_name = "SoA Chunks CFM Sync (impl 14)";
    cfg.initial_blocks = 10000;
    cfg.additional_blocks = 1'500'000;  // > 1M to cross into second chunk
    cfg.num_writers = 4;                // Multiple writers - no spinlock serialization!
    cfg.num_readers = 8;                // Stress test for TSan
    cfg.traversal_depth = 500;
    cfg.test_duration_ms = 30000;       // 30 seconds

    fmt::print("  CHUNK_SIZE = {} (~1M)\n", soa_chunks_cfm_sync::CHUNK_SIZE);
    fmt::print("  Total blocks = {} (will use 2 chunks)\n", cfg.initial_blocks + cfg.additional_blocks);
    fmt::print("  NOTE: Multiple writers (no spinlock) - testing CFM bucket sync!\n\n");

    soa_chunks_cfm_sync::block_index_store store;
    store.preallocate(cfg.initial_blocks + cfg.additional_blocks);

    run_tsan_test(
        cfg,
        store,
        // safe_add: uses insert_and_visit with bucket locking
        [](auto& s, auto const& hash, auto const& hdr) {
            return s.add(hash, hdr);
        },
        // writer_fn: same as safe_add (no spinlock, relies on CFM)
        [](auto& s, auto const& hash, auto const& hdr) {
            s.add(hash, hdr);
        },
        // cache_fn: lock-free lookup via concurrent_flat_map
        [](auto& s, auto const& hash) -> uint32_t {
            return s.find_idx(hash);
        },
        // reader_fn: use traverse_back (testing CFM sync + transitivity)
        [](auto& s, uint32_t start_idx, int depth) {
            int count = 0;
            s.traverse_back(start_idx, depth, [&](uint32_t /*timestamp*/) {
                ++count;
            });
            ankerl::nanobench::doNotOptimizeAway(count);
        }
    );

    fmt::print("\nSoA Chunks CFM Sync: CFM bucket locking + happens-before transitivity.\n");
    fmt::print("Final size: {} (crossed to chunk {})\n", store.size(), store.size() / soa_chunks_cfm_sync::CHUNK_SIZE);
    fmt::print("If NO TSan errors: CFM bucket sync + transitivity is SUFFICIENT!\n");
    fmt::print("If TSan reports races: CFM bucket lock does NOT synchronize external writes!\n");
}

// =============================================================================
// SoA Static (impl 17): Static array allocation, same sync as impl 14
// Simpler than chunks - single array per field
// =============================================================================
void test_soa_static_concurrent() {
    tsan_test_config cfg;
    cfg.test_name = "SoA Static (impl 17)";
    cfg.initial_blocks = 10000;
    cfg.additional_blocks = 500000;  // Limited by static capacity
    cfg.num_writers = 4;             // Multiple writers
    cfg.num_readers = 8;
    cfg.traversal_depth = 500;
    cfg.test_duration_ms = 30000;

    fmt::print("  capacity = {} blocks\n", soa_static::default_capacity);
    fmt::print("  warn_threshold = {} blocks (95%%)\n", (soa_static::default_capacity * 95) / 100);
    fmt::print("  Total blocks = {}\n\n", cfg.initial_blocks + cfg.additional_blocks);

    soa_static::block_index_store store;

    run_tsan_test(
        cfg,
        store,
        // safe_add: uses insert_and_visit with bucket locking
        [](auto& s, auto const& hash, auto const& hdr) {
            auto [inserted, idx, warn] = s.add(hash, hdr);
            return idx;
        },
        // writer_fn: same as safe_add
        [](auto& s, auto const& hash, auto const& hdr) {
            s.add(hash, hdr);
        },
        // cache_fn: lock-free lookup via concurrent_flat_map
        [](auto& s, auto const& hash) -> uint32_t {
            return s.find_idx(hash);
        },
        // reader_fn: use traverse_back
        [](auto& s, uint32_t start_idx, int depth) {
            int count = 0;
            s.traverse_back(start_idx, depth, [&](uint32_t /*timestamp*/) {
                ++count;
            });
            ankerl::nanobench::doNotOptimizeAway(count);
        }
    );

    fmt::print("\nSoA Static: Static arrays + CFM bucket locking.\n");
    fmt::print("Final size: {}\n", store.size());
    fmt::print("If NO TSan errors: SoA Static is thread-safe!\n");
}

// =============================================================================
// PROPER API USAGE TEST (BCHN-style)
// This test uses the API CORRECTLY - holding cs_main for the entire operation.
// Should have NO data races when using the API as intended.
// =============================================================================

void test_bchn_api_correct_usage() {
    fmt::print("\n========== BCHN API TEST (correct usage) ==========\n");
    fmt::print("Testing BCHN-style API with proper locking (like real BCHN).\n");
    fmt::print("Should have NO data races if API is used correctly.\n\n");

    constexpr size_t num_blocks = 10000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_readers = 4;

    auto chain = generate_chain(num_blocks);

    // Store hashes for lookup
    std::vector<hash_digest> hashes;
    hashes.reserve(num_blocks);
    for (auto const& [hash, _] : chain) {
        hashes.push_back(hash);
    }

    bchn_style::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};

    // Pre-insert all blocks (with proper locking)
    {
        std::unique_lock lock(store.cs_main());
        for (auto const& [hash, hdr] : chain) {
            store.add_to_block_index(hash, hdr);
        }
    }

    std::vector<std::thread> threads;

    // Writers: modify status WITH lock held (correct usage)
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &hashes, &stop, &write_count, w] {
            std::mt19937 rng(w);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // CORRECT: Hold lock for entire operation
                std::unique_lock lock(store.cs_main());
                auto* block = store.lookup_block_index(hashes[dist(rng)]);
                if (block) {
                    // SAFE: lock is held, can modify status
                    block->status = uint32_t(w + 1);
                    ++write_count;
                }
            }
        });
    }

    // Readers: read status WITH lock held (correct usage)
    for (size_t r = 0; r < num_readers; ++r) {
        threads.emplace_back([&store, &hashes, &stop, &read_count, r] {
            std::mt19937 rng(100 + r);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // CORRECT: Hold lock for entire operation
                std::shared_lock lock(store.cs_main());
                auto const* block = store.lookup_block_index(hashes[dist(rng)]);
                if (block) {
                    // SAFE: lock is held, can read status
                    volatile uint32_t s = block->status;
                    (void)s;
                    ++read_count;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", write_count.load());
    fmt::print("  Reads:  {}\n", read_count.load());
    fmt::print("\nIf no TSan errors appeared, the API is being used correctly!\n");
}

// =============================================================================
// INCORRECT API USAGE TEST (for comparison - WILL have data races)
// This test intentionally MISUSES the API to show what happens.
// =============================================================================

void test_bchn_api_incorrect_usage() {
    fmt::print("\n========== BCHN API TEST (INCORRECT usage - expect races) ==========\n");
    fmt::print("Testing BCHN-style API WITHOUT proper locking.\n");
    fmt::print("This SHOULD trigger TSan errors to demonstrate incorrect usage.\n\n");

    constexpr size_t num_blocks = 10000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_readers = 4;

    auto chain = generate_chain(num_blocks);

    std::vector<hash_digest> hashes;
    hashes.reserve(num_blocks);
    for (auto const& [hash, _] : chain) {
        hashes.push_back(hash);
    }

    bchn_style::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};

    // Pre-insert
    for (auto const& [hash, hdr] : chain) {
        store.add(hash, hdr);
    }

    std::vector<std::thread> threads;

    // Writers: INCORRECTLY release lock before using pointer
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &hashes, &stop, &write_count, w] {
            std::mt19937 rng(w);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // WRONG: using convenience find() that releases lock immediately
                auto* block = store.find(hashes[dist(rng)]);
                if (block) {
                    // DATA RACE: lock was released, but we're still using the pointer!
                    block->status = uint32_t(w + 1);
                    ++write_count;
                }
            }
        });
    }

    // Readers: INCORRECTLY release lock before using pointer
    for (size_t r = 0; r < num_readers; ++r) {
        threads.emplace_back([&store, &hashes, &stop, &read_count, r] {
            std::mt19937 rng(100 + r);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // WRONG: using convenience find() that releases lock immediately
                auto* block = store.find(hashes[dist(rng)]);
                if (block) {
                    // DATA RACE: lock was released!
                    volatile uint32_t s = block->status;
                    (void)s;
                    ++read_count;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", write_count.load());
    fmt::print("  Reads:  {}\n", read_count.load());
    fmt::print("\nIf TSan reported races above, it's because the API was misused.\n");
}

// =============================================================================
// CONCURRENT NODE MAP TESTS
// =============================================================================

void test_concurrent_node_api_correct_usage() {
    fmt::print("\n========== CONCURRENT NODE MAP TEST (correct usage) ==========\n");
    fmt::print("Testing concurrent_node_map API with proper visit() usage.\n");
    fmt::print("Should have NO data races if API is used correctly.\n\n");

    constexpr size_t num_blocks = 10000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_readers = 4;

    auto chain = generate_chain(num_blocks);

    std::vector<hash_digest> hashes;
    hashes.reserve(num_blocks);
    for (auto const& [hash, _] : chain) {
        hashes.push_back(hash);
    }

    concurrent_node::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};

    // Pre-insert all blocks
    for (auto const& [hash, hdr] : chain) {
        store.add_to_block_index(hash, hdr);
    }

    std::vector<std::thread> threads;

    // Writers: modify status through visit() (correct usage)
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &hashes, &stop, &write_count, w] {
            std::mt19937 rng(w);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // CORRECT: Use set_status() which uses visit() internally
                if (store.set_status(hashes[dist(rng)], uint32_t(w + 1))) {
                    ++write_count;
                }
            }
        });
    }

    // Readers: read status through visit() (correct usage)
    for (size_t r = 0; r < num_readers; ++r) {
        threads.emplace_back([&store, &hashes, &stop, &read_count, r] {
            std::mt19937 rng(100 + r);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // CORRECT: Use get_status() which uses visit() internally
                uint32_t status;
                if (store.get_status(hashes[dist(rng)], status)) {
                    ankerl::nanobench::doNotOptimizeAway(status);
                    ++read_count;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", write_count.load());
    fmt::print("  Reads:  {}\n", read_count.load());
    fmt::print("\nIf no TSan errors appeared, the API is being used correctly!\n");
}

void test_concurrent_node_api_incorrect_usage() {
    fmt::print("\n========== CONCURRENT NODE MAP TEST (INCORRECT usage - expect races) ==========\n");
    fmt::print("Testing concurrent_node_map API with raw pointers OUTSIDE visit().\n");
    fmt::print("This SHOULD trigger TSan errors to demonstrate incorrect usage.\n\n");

    constexpr size_t num_blocks = 10000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_readers = 4;

    auto chain = generate_chain(num_blocks);

    std::vector<hash_digest> hashes;
    hashes.reserve(num_blocks);
    for (auto const& [hash, _] : chain) {
        hashes.push_back(hash);
    }

    concurrent_node::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};

    // Pre-insert
    for (auto const& [hash, hdr] : chain) {
        store.add(hash, hdr);
    }

    std::vector<std::thread> threads;

    // Writers: INCORRECTLY use raw pointer outside visit()
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &hashes, &stop, &write_count, w] {
            std::mt19937 rng(w);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // WRONG: find() returns raw pointer, visit() has already ended
                auto* block = store.find(hashes[dist(rng)]);
                if (block) {
                    // DATA RACE: accessing block outside of visit()!
                    block->status = uint32_t(w + 1);
                    ++write_count;
                }
            }
        });
    }

    // Readers: INCORRECTLY use raw pointer outside visit()
    for (size_t r = 0; r < num_readers; ++r) {
        threads.emplace_back([&store, &hashes, &stop, &read_count, r] {
            std::mt19937 rng(100 + r);
            std::uniform_int_distribution<size_t> dist(0, hashes.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // WRONG: find() returns raw pointer
                auto* block = store.find(hashes[dist(rng)]);
                if (block) {
                    // DATA RACE: accessing block outside of visit()!
                    volatile uint32_t s = block->status;
                    (void)s;
                    ++read_count;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", write_count.load());
    fmt::print("  Reads:  {}\n", read_count.load());
    fmt::print("\nIf TSan reported races above, it's because the API was misused.\n");
}

// =============================================================================
// PARENT TRAVERSAL THREAD-SAFETY TESTS
// Test if traversing pprev while inserting is thread-safe
// =============================================================================

void test_bchn_traversal_thread_safety() {
    fmt::print("\n========== BCHN TRAVERSAL TEST (with proper locking) ==========\n");
    fmt::print("Testing BCHN-style traversal while other threads insert.\n");
    fmt::print("Traversal requires holding cs_main lock.\n\n");

    constexpr size_t initial_blocks = 100000;
    constexpr size_t additional_blocks = 100000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_traversers = 8;
    constexpr int traversal_depth = 500;
    constexpr int test_duration_ms = 1000;

    fmt::print("  Config: {} initial + {} additional blocks\n", initial_blocks, additional_blocks);
    fmt::print("  Config: {} writers, {} traversers, depth {}\n", num_writers, num_traversers, traversal_depth);
    fmt::print("  Config: {} ms duration\n\n", test_duration_ms);

    auto chain = generate_chain(initial_blocks + additional_blocks);

    bchn_style::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> traverse_count{0};
    std::atomic<size_t> insert_count{0};

    // Pre-insert initial blocks
    {
        std::unique_lock lock(store.cs_main());
        for (size_t i = 0; i < initial_blocks; ++i) {
            store.add_to_block_index(chain[i].first, chain[i].second);
        }
    }

    std::vector<std::thread> threads;

    // Writers: insert remaining blocks
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &chain, &stop, &insert_count, w, initial_blocks, additional_blocks, num_writers] {
            size_t start = initial_blocks + (w * additional_blocks / num_writers);
            size_t end = initial_blocks + ((w + 1) * additional_blocks / num_writers);
            for (size_t i = start; i < end && !stop.load(std::memory_order_relaxed); ++i) {
                std::unique_lock lock(store.cs_main());
                store.add_to_block_index(chain[i].first, chain[i].second);
                ++insert_count;
            }
        });
    }

    // Traversers: walk back through parents WITH lock held
    for (size_t t = 0; t < num_traversers; ++t) {
        threads.emplace_back([&store, &chain, &stop, &traverse_count, t, initial_blocks, traversal_depth] {
            std::mt19937 rng(t);
            std::uniform_int_distribution<size_t> dist(traversal_depth, initial_blocks - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // CORRECT: Hold lock for entire traversal
                std::shared_lock lock(store.cs_main());
                auto* idx = store.lookup_block_index(chain[dist(rng)].first);
                int count = 0;
                while (idx != nullptr && count < traversal_depth) {
                    idx = idx->pprev;  // Safe: lock is held
                    ++count;
                }
                ++traverse_count;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Inserts:    {}\n", insert_count.load());
    fmt::print("  Traversals: {}\n", traverse_count.load());
    fmt::print("\nIf no TSan errors, BCHN-style traversal with locking is thread-safe.\n");
}

void test_bchn_traversal_unlock_after_lookup() {
    fmt::print("\n========== BCHN TRAVERSAL TEST (lock only for lookup) ==========\n");
    fmt::print("Testing BCHN-style: lock for initial lookup, unlock before traversal.\n");
    fmt::print("std::unordered_map guarantees pointer stability on insert.\n");
    fmt::print("pprev is set once at insertion and never modified.\n\n");

    constexpr size_t initial_blocks = 100000;
    constexpr size_t additional_blocks = 100000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_traversers = 8;
    constexpr int traversal_depth = 500;
    constexpr int test_duration_ms = 1000;

    fmt::print("  Config: {} initial + {} additional blocks\n", initial_blocks, additional_blocks);
    fmt::print("  Config: {} writers, {} traversers, depth {}\n", num_writers, num_traversers, traversal_depth);
    fmt::print("  Config: {} ms duration\n\n", test_duration_ms);

    auto chain = generate_chain(initial_blocks + additional_blocks);

    bchn_style::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> traverse_count{0};
    std::atomic<size_t> insert_count{0};

    // Pre-insert initial blocks
    {
        std::unique_lock lock(store.cs_main());
        for (size_t i = 0; i < initial_blocks; ++i) {
            store.add_to_block_index(chain[i].first, chain[i].second);
        }
    }

    std::vector<std::thread> threads;

    // Writers: insert remaining blocks
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &chain, &stop, &insert_count, w, initial_blocks, additional_blocks, num_writers] {
            size_t start = initial_blocks + (w * additional_blocks / num_writers);
            size_t end = initial_blocks + ((w + 1) * additional_blocks / num_writers);
            for (size_t i = start; i < end && !stop.load(std::memory_order_relaxed); ++i) {
                std::unique_lock lock(store.cs_main());
                store.add_to_block_index(chain[i].first, chain[i].second);
                ++insert_count;
            }
        });
    }

    // Traversers: lock ONLY for lookup, then traverse WITHOUT lock
    for (size_t t = 0; t < num_traversers; ++t) {
        threads.emplace_back([&store, &chain, &stop, &traverse_count, t, initial_blocks, traversal_depth] {
            std::mt19937 rng(t);
            std::uniform_int_distribution<size_t> dist(traversal_depth, initial_blocks - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                bchn_style::block_index* start_idx;
                {
                    // Lock ONLY for the hash lookup
                    std::shared_lock lock(store.cs_main());
                    start_idx = store.lookup_block_index(chain[dist(rng)].first);
                }
                // Lock released - now traverse without lock
                // Safe because:
                // 1. std::unordered_map guarantees pointer stability on insert
                // 2. pprev is set once at insertion and never modified
                // 3. We only follow pprev to "old" blocks that existed before our lookup
                int count = 0;
                auto* idx = start_idx;
                while (idx != nullptr && count < traversal_depth) {
                    idx = idx->pprev;
                    ++count;
                }
                ++traverse_count;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Inserts:    {}\n", insert_count.load());
    fmt::print("  Traversals: {}\n", traverse_count.load());
    fmt::print("\nIf no TSan errors, lock-only-for-lookup pattern is thread-safe.\n");
    fmt::print("This is safe because pprev only points to OLDER blocks.\n");
}

void test_concurrent_node_traversal_thread_safety() {
    fmt::print("\n========== CONCURRENT NODE MAP TRAVERSAL TEST ==========\n");
    fmt::print("Testing concurrent_node_map traversal (via pprev) while inserting.\n");
    fmt::print("WARNING: This tests if pprev traversal is safe during concurrent inserts.\n\n");

    constexpr size_t initial_blocks = 100000;
    constexpr size_t additional_blocks = 100000;
    constexpr size_t num_writers = 4;
    constexpr size_t num_traversers = 8;
    constexpr int traversal_depth = 500;
    constexpr int test_duration_ms = 1000;

    fmt::print("  Config: {} initial + {} additional blocks\n", initial_blocks, additional_blocks);
    fmt::print("  Config: {} writers, {} traversers, depth {}\n", num_writers, num_traversers, traversal_depth);
    fmt::print("  Config: {} ms duration\n\n", test_duration_ms);

    auto chain = generate_chain(initial_blocks + additional_blocks);

    concurrent_node::block_index_store store;
    std::atomic<bool> stop{false};
    std::atomic<size_t> traverse_count{0};
    std::atomic<size_t> insert_count{0};

    // Pre-insert initial blocks
    for (size_t i = 0; i < initial_blocks; ++i) {
        store.add(chain[i].first, chain[i].second);
    }

    // Store pointers to some blocks for traversal starting points
    std::vector<concurrent_node::block_index*> start_points;
    for (size_t i = traversal_depth; i < initial_blocks; i += 100) {
        start_points.push_back(store.find(chain[i].first));
    }

    std::vector<std::thread> threads;

    // Writers: insert remaining blocks (extends existing chains)
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &chain, &stop, &insert_count, w, initial_blocks, additional_blocks, num_writers] {
            size_t start = initial_blocks + (w * additional_blocks / num_writers);
            size_t end = initial_blocks + ((w + 1) * additional_blocks / num_writers);
            for (size_t i = start; i < end && !stop.load(std::memory_order_relaxed); ++i) {
                store.add(chain[i].first, chain[i].second);
                ++insert_count;
            }
        });
    }

    // Traversers: walk back through pprev WITHOUT external lock
    // This tests if concurrent_node_map's pointer stability is sufficient
    for (size_t t = 0; t < num_traversers; ++t) {
        threads.emplace_back([&start_points, &stop, &traverse_count, t, traversal_depth] {
            std::mt19937 rng(t);
            std::uniform_int_distribution<size_t> dist(0, start_points.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // Traversing pprev pointers without lock
                // concurrent_node_map guarantees pointer stability, but is this safe?
                auto* idx = start_points[dist(rng)];
                int count = 0;
                while (idx != nullptr && count < traversal_depth) {
                    // Reading pprev (set at insertion time, never modified after)
                    idx = idx->pprev;
                    ++count;
                }
                ++traverse_count;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Inserts:    {}\n", insert_count.load());
    fmt::print("  Traversals: {}\n", traverse_count.load());
    fmt::print("\nIf TSan reports races, pprev traversal needs synchronization!\n");
    fmt::print("If no TSan errors, pointer stability + immutable pprev is sufficient.\n");
}

// void test_cfm_hash_traversal_thread_safety() {
//     fmt::print("\n========== CFM-HASH TRAVERSAL TEST ==========\n");
//     fmt::print("Testing CFM-Hash traversal (via hash lookup) while inserting.\n");
//     fmt::print("CFM-Hash uses hash references instead of pointers - should be inherently safe.\n\n");

//     constexpr size_t initial_blocks = 100000;
//     constexpr size_t additional_blocks = 100000;
//     constexpr size_t num_writers = 4;
//     constexpr size_t num_traversers = 8;
//     constexpr int traversal_depth = 500;
//     constexpr int test_duration_ms = 1000;

//     fmt::print("  Config: {} initial + {} additional blocks\n", initial_blocks, additional_blocks);
//     fmt::print("  Config: {} writers, {} traversers, depth {}\n", num_writers, num_traversers, traversal_depth);
//     fmt::print("  Config: {} ms duration\n\n", test_duration_ms);

//     auto chain = generate_chain(initial_blocks + additional_blocks);

//     cfm_hash::block_index_store store;
//     std::atomic<bool> stop{false};
//     std::atomic<size_t> traverse_count{0};
//     std::atomic<size_t> insert_count{0};

//     // Pre-insert initial blocks
//     for (size_t i = 0; i < initial_blocks; ++i) {
//         store.add(chain[i].first, chain[i].second);
//     }

//     // Store hashes for traversal starting points (not pointers!)
//     std::vector<hash_digest> start_hashes;
//     for (size_t i = traversal_depth; i < initial_blocks; i += 100) {
//         start_hashes.push_back(chain[i].first);
//     }

//     std::vector<std::thread> threads;

//     // Writers: insert remaining blocks
//     for (size_t w = 0; w < num_writers; ++w) {
//         threads.emplace_back([&store, &chain, &stop, &insert_count, w, initial_blocks, additional_blocks, num_writers] {
//             size_t start = initial_blocks + (w * additional_blocks / num_writers);
//             size_t end = initial_blocks + ((w + 1) * additional_blocks / num_writers);
//             for (size_t i = start; i < end && !stop.load(std::memory_order_relaxed); ++i) {
//                 store.add(chain[i].first, chain[i].second);
//                 ++insert_count;
//             }
//         });
//     }

//     // Traversers: walk back through parents via HASH LOOKUPS (no pointers!)
//     for (size_t t = 0; t < num_traversers; ++t) {
//         threads.emplace_back([&store, &start_hashes, &stop, &traverse_count, t, traversal_depth] {
//             std::mt19937 rng(t);
//             std::uniform_int_distribution<size_t> dist(0, start_hashes.size() - 1);
//             while (!stop.load(std::memory_order_relaxed)) {
//                 // Traversing via hash lookups - no pointer stability needed!
//                 int count = store.traverse_back(start_hashes[dist(rng)], traversal_depth);
//                 ankerl::nanobench::doNotOptimizeAway(count);
//                 ++traverse_count;
//             }
//         });
//     }

//     std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
//     stop = true;

//     for (auto& th : threads) {
//         th.join();
//     }

//     fmt::print("  Inserts:    {}\n", insert_count.load());
//     fmt::print("  Traversals: {}\n", traverse_count.load());
//     fmt::print("\nCFM-Hash uses visit() for all access - should be thread-safe by design.\n");
// }

void test_soa_traversal_unlock_after_lookup() {
    fmt::print("\n========== SOA TRAVERSAL TEST (cached index race) ==========\n");
    fmt::print("Testing SoA: cache indices BEFORE inserts, then traverse while vector resizes.\n");
    fmt::print("WARNING: std::vector does NOT guarantee pointer stability on resize!\n");
    fmt::print("If vector reallocates during traversal, we have a DATA RACE.\n\n");

    // Strategy: pre-cache indices from initial blocks, then traverse using those
    // cached indices while writers insert more blocks (causing resizes).
    // The race: traverser reads parent_indices_[cached_idx] while writer resizes.
    constexpr size_t initial_blocks = 1000;       // Small initial set
    constexpr size_t additional_blocks = 500000;  // Large set to force ~15-20 resizes
    constexpr size_t num_writers = 4;
    constexpr size_t num_traversers = 8;
    constexpr int traversal_depth = 100;          // Shorter depth for fewer initial blocks
    constexpr int test_duration_ms = 2000;        // Longer to increase race chance

    fmt::print("  Config: {} initial + {} additional blocks\n", initial_blocks, additional_blocks);
    fmt::print("  Config: {} writers, {} traversers, depth {}\n", num_writers, num_traversers, traversal_depth);
    fmt::print("  Config: {} ms duration\n", test_duration_ms);
    fmt::print("  Config: NO preallocate - vectors WILL resize during test!\n");
    fmt::print("  Expected resizes: ~15-20 (1000 -> 500000 with 1.5x growth)\n\n");

    auto chain = generate_chain(initial_blocks + additional_blocks);

    soa_fully::block_index_store store;
    // Intentionally NOT calling preallocate() to force resizes during test
    // store.preallocate(initial_blocks + additional_blocks);

    std::atomic<bool> stop{false};
    std::atomic<size_t> traverse_count{0};
    std::atomic<size_t> insert_count{0};

    // Pre-insert initial blocks (small set, vector capacity ~1500 after this)
    for (size_t i = 0; i < initial_blocks; ++i) {
        store.add(chain[i].first, chain[i].second);
    }

    // PRE-CACHE indices BEFORE starting writers
    // These indices point into the vector's current memory location.
    // When writers resize the vector, the memory moves but our cached indices
    // still point to the OLD memory (use-after-free / data race).
    std::vector<uint32_t> cached_indices;
    cached_indices.reserve(initial_blocks);
    for (size_t i = 0; i < initial_blocks; ++i) {
        cached_indices.push_back(store.find_idx(chain[i].first));
    }
    fmt::print("  Pre-cached {} indices BEFORE starting writers\n\n", cached_indices.size());

    std::vector<std::thread> threads;

    // Writers: insert remaining blocks - this will cause vector resizes!
    for (size_t w = 0; w < num_writers; ++w) {
        threads.emplace_back([&store, &chain, &stop, &insert_count, w, initial_blocks, additional_blocks, num_writers] {
            size_t start = initial_blocks + (w * additional_blocks / num_writers);
            size_t end = initial_blocks + ((w + 1) * additional_blocks / num_writers);
            for (size_t i = start; i < end && !stop.load(std::memory_order_relaxed); ++i) {
                store.add(chain[i].first, chain[i].second);
                ++insert_count;
            }
        });
    }

    // Traversers: use PRE-CACHED indices (NO find_idx call, NO lock synchronization!)
    // This is the race: we traverse using indices obtained BEFORE any resize
    for (size_t t = 0; t < num_traversers; ++t) {
        threads.emplace_back([&store, &cached_indices, &stop, &traverse_count, t, traversal_depth] {
            std::mt19937 rng(t);
            std::uniform_int_distribution<size_t> dist(static_cast<size_t>(traversal_depth), cached_indices.size() - 1);
            while (!stop.load(std::memory_order_relaxed)) {
                // Use CACHED index - no lock, no synchronization with writers!
                uint32_t start_idx = cached_indices[dist(rng)];
                if (start_idx == soa_fully::null_index) continue;

                // Traverse WITHOUT lock - DANGEROUS if vector resizes!
                // This accesses parent_indices_[idx] which may be invalidated
                int count = 0;
                uint32_t idx = start_idx;
                while (idx != soa_fully::null_index && count < traversal_depth) {
                    idx = store.get_parent_idx(idx);  // Direct vector access - RACE!
                    ++count;
                }
                ++traverse_count;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Inserts:    {}\n", insert_count.load());
    fmt::print("  Traversals: {}\n", traverse_count.load());
    fmt::print("\nIf TSan reports races, SoA with vectors is NOT safe without locks!\n");
    fmt::print("Unlike unordered_map, vector resize invalidates ALL pointers/references.\n");
}

// More aggressive test: use add_unsafe() to completely remove locks during insert
void test_soa_vector_race_unsafe() {
    fmt::print("\n========== SOA VECTOR RACE TEST (1 writer vs N readers) ==========\n");
    fmt::print("Testing: single writer with add_unsafe() + concurrent readers\n");
    fmt::print("All threads start simultaneously for maximum overlap.\n\n");

    constexpr size_t num_readers = 8;
    constexpr size_t total_blocks = 200000;
    constexpr int test_duration_ms = 5000;

    fmt::print("  Config: 1 writer, {} blocks\n", total_blocks);
    fmt::print("  Config: {} readers\n", num_readers);
    fmt::print("  Config: {} ms duration, NO locks on writer\n\n", test_duration_ms);

    auto chain = generate_chain(total_blocks);

    soa_fully::block_index_store store;
    // NO preallocate - force resizes

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop{false};
    std::atomic<size_t> write_count{0};
    std::atomic<size_t> read_count{0};

    std::vector<std::thread> threads;

    // Single writer using add_unsafe() - NO LOCK
    threads.emplace_back([&store, &chain, &start_flag, &stop, &write_count, total_blocks] {
        while (!start_flag.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
        for (size_t i = 0; i < total_blocks && !stop.load(std::memory_order_relaxed); ++i) {
            store.add_unsafe(chain[i].first, chain[i].second);  // NO LOCK!
            ++write_count;
            // Small delay to increase overlap with readers
            if (i % 1000 == 0) {
                std::this_thread::yield();
            }
        }
    });

    // Multiple readers - read while writer is inserting
    for (size_t r = 0; r < num_readers; ++r) {
        threads.emplace_back([&store, &start_flag, &stop, &read_count, r] {
            std::mt19937 rng(unsigned(r));
            while (!start_flag.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_relaxed)) {
                // Read size WITHOUT lock (races with push_back)
                size_t sz = store.size();
                if (sz == 0) continue;

                // Read random index (races with push_back and resize)
                uint32_t idx = rng() % uint32_t(sz);
                uint32_t parent = store.get_parent_idx(idx);
                (void)parent;

                // Also read height (another vector access)
                int32_t height = store.get_height(idx);
                (void)height;

                ++read_count;
            }
        });
    }

    // Start all threads simultaneously
    fmt::print("  Starting 1 writer + {} readers simultaneously...\n", num_readers);
    start_flag = true;

    // Let it run
    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop = true;

    for (auto& th : threads) {
        th.join();
    }

    fmt::print("  Writes: {}\n", write_count.load());
    fmt::print("  Reads:  {}\n", read_count.load());
    fmt::print("\nTSan should report races: reader vector access vs writer push_back()\n");
}

void benchmark_memory_layout() {
    fmt::print("\n========== MEMORY LAYOUT INFO ==========\n");

    fmt::print("sizeof(hash_digest):                    {} bytes\n", sizeof(hash_digest));
    fmt::print("sizeof(header_data):                    {} bytes\n", sizeof(header_data));
    fmt::print("\n");
    fmt::print("BCHN-style block_index:                 {} bytes\n", sizeof(bchn_style::block_index));
    fmt::print("Index-based block_index:                {} bytes\n", sizeof(index_based::block_index));
    // fmt::print("Index-based preallocated block_index:   {} bytes\n", sizeof(index_based_preallocated::block_index));
    // fmt::print("Vector AoS+Ptr block_index:             {} bytes\n", sizeof(vector_aos_ptr::block_index));
    // fmt::print("Vector AoS+Ptr Compact block_index:     {} bytes\n", sizeof(vector_aos_ptr_compact::block_index));
    fmt::print("Concurrent node map block_index:        {} bytes\n", sizeof(concurrent_node::block_index));
    // fmt::print("CFM-Hash block_index:                   {} bytes\n", sizeof(cfm_hash::block_index));
    fmt::print("\n");
    fmt::print("Knuth block_entry:                      {} bytes\n", sizeof(kth::blockchain::block_entry));
    fmt::print("\n");
}

// Isolated test for profiling - run with: ./block_index_store_benchmarks profile
// void profile_vector_traversal() {
//     fmt::print("\n========== PROFILING Vector+Map traversal ==========\n");
//     fmt::print("Running 10 million traversals for profiling...\n\n");

//     constexpr size_t num_blocks = 10000;
//     auto chain = generate_chain(num_blocks);

//     index_based_preallocated::block_index_store vec_store;
//     vec_store.preallocate(num_blocks * 3 / 2);

//     for (auto const& [hash, hdr] : chain) {
//         vec_store.add(hash, hdr);
//     }

//     uint32_t tip_idx = uint32_t(num_blocks - 1);

//     fmt::print("Starting traversals...\n");

//     volatile int total = 0;
//     for (int i = 0; i < 10'000'000; ++i) {
//         total += vec_store.traverse_back_by_index_unsafe(tip_idx, 100);
//     }

//     fmt::print("Done. Total: {}\n", total);
// }

void profile_pointer_traversal() {
    fmt::print("\n========== PROFILING BCHN-style traversal ==========\n");
    fmt::print("Running 10 million traversals for profiling...\n\n");

    constexpr size_t num_blocks = 10000;
    auto chain = generate_chain(num_blocks);

    bchn_style::block_index_store bchn_store;

    for (auto const& [hash, hdr] : chain) {
        bchn_store.add(hash, hdr);
    }

    hash_digest const& tip_hash = chain.back().first;

    fmt::print("Starting traversals...\n");

    volatile int total = 0;
    for (int i = 0; i < 10'000'000; ++i) {
        auto* idx = bchn_store.find(tip_hash);
        int count = 0;
        while (idx != nullptr && count < 100) {
            idx = idx->pprev;
            ++count;
        }
        total += count;
    }

    fmt::print("Done. Total: {}\n", total);
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    // Check for profiling mode
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "profile-vector") {
            // profile_vector_traversal();
            return 0;
        } else if (arg == "profile-pointer") {
            profile_pointer_traversal();
            return 0;
        }
    }

    fmt::print("==============================================\n");
    fmt::print("  Block Index Store Benchmarks\n");
    fmt::print("  Comparing 5 implementations:\n");
    fmt::print("  1. BCHN-style (unordered_map + mutex)\n");
    fmt::print("  2. Index-based (flat_map + vector)\n");
    fmt::print("  3. Concurrent node map (pointer-based)\n");
    fmt::print("  4. CFM-Hash (concurrent_flat_map + hash refs)\n");
    fmt::print("  5. Knuth block_pool (bimap + upgrade_mutex)\n");
    fmt::print("==============================================\n");

#if defined(TSAN_BUILD)
    // TSan build: run thread-safety tests
    fmt::print("\n*** TSan BUILD - Running thread-safety tests ***\n");

    // New generic tests with start_flag synchronization
    // test_bchn_pointer_stability();    // BCHN: should be SAFE (pointer stability)
    // test_soa_pointer_instability();   // SoA vector: should FAIL (vector resize)
    // test_soa_deque_stability();       // SoA deque: FAILS (internal map race)
    // test_soa_chunks_lock_free();            // SoA chunks: should be SAFE (fixed directory + immutable chunks)
    // test_soa_chunks_lockfree_concurrent();  // SoA chunks LF: fully lock-free reads (concurrent_flat_map + CAS)
    // test_soa_chunks_cfm_sync_concurrent();  // SoA chunks CFM sync: CFM bucket locking (impl 14)
    test_soa_static_concurrent();           // SoA static: static arrays (impl 17)

    // Old tests (kept for reference)
    // test_bchn_api_correct_usage();
    // test_bchn_api_incorrect_usage();
    // test_concurrent_node_api_correct_usage();
    // test_concurrent_node_api_incorrect_usage();
    // test_bchn_traversal_thread_safety();
    // test_bchn_traversal_unlock_after_lookup();
    // test_concurrent_node_traversal_thread_safety();
    // test_cfm_hash_traversal_thread_safety();
    // test_soa_traversal_unlock_after_lookup();
    // test_soa_vector_race_unsafe();

    fmt::print("\n==============================================\n");
    fmt::print("  Thread-Safety Tests Complete!\n");
    fmt::print("==============================================\n");

#elif defined(BENCHMARK_BUILD)
    // Benchmark build: run performance benchmarks (no block_pool in concurrent tests)
    fmt::print("\n*** BENCHMARK BUILD - Running performance benchmarks ***\n");

    benchmark_memory_layout();
    benchmark_sequential_insert();
    benchmark_concurrent_insert();
    benchmark_lookup();


    // Realistic traversal benchmarks based on BCHN usage patterns
    // Using the 3 primitives: traverse_back, get_ancestor, find_common_ancestor
    benchmark_get_median_time_past();        // traverse_back(11, visitor) - most frequent
    benchmark_finalization_check_skip();     // get_ancestor_skip(height - 10) - O(log n)
    benchmark_finalization_check_linear();   // get_ancestor_linear(height - 10) - O(n)
    benchmark_daa_get_ancestor_skip();       // get_ancestor_skip(height - 144) - O(log n)
    benchmark_daa_get_ancestor_linear();     // get_ancestor_linear(height - 144) - O(n)
    benchmark_find_common_ancestor();        // find_common_ancestor(a, b) - reorgs

    // benchmark_lookup();



    fmt::print("\n==============================================\n");
    fmt::print("  Benchmarks Complete!\n");
    fmt::print("==============================================\n");

#else
    // Default: run basic benchmarks + realistic scenarios
    fmt::print("\n*** DEFAULT BUILD ***\n");
    benchmark_memory_layout();
    benchmark_sequential_insert();

    // Realistic traversal benchmarks based on BCHN usage patterns
    benchmark_get_median_time_past();        // traverse_back(11, visitor) - most frequent
    benchmark_finalization_check_skip();     // get_ancestor_skip(height - 10) - O(log n)
    benchmark_finalization_check_linear();   // get_ancestor_linear(height - 10) - O(n)
    benchmark_daa_get_ancestor_skip();       // get_ancestor_skip(height - 144) - O(log n)
    benchmark_daa_get_ancestor_linear();     // get_ancestor_linear(height - 144) - O(n)
    benchmark_find_common_ancestor();        // find_common_ancestor(a, b) - reorgs

#endif

    return 0;
}
