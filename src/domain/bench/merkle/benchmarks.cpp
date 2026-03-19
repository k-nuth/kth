// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <fmt/core.h>

#include <random>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <crypto/sha256.h>

#include "implementations.hpp"

using namespace kth;
using namespace kth::bench::merkle;
using ankerl::nanobench::Bench;

// =============================================================================
// Test Data Generation
// =============================================================================

// Generate a list of random hashes (simulating transaction hashes)
hash_list generate_random_hashes(size_t count, uint32_t seed = 12345) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    hash_list hashes;
    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        hash_digest hash;
        for (auto& byte : hash) {
            byte = dist(rng);
        }
        hashes.push_back(hash);
    }

    return hashes;
}

// =============================================================================
// Correctness Verification
// =============================================================================

bool verify_implementations() {
    fmt::print("Verifying implementation correctness...\n");

    std::vector<size_t> test_sizes = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 100, 1000, 10000};

    for (auto size : test_sizes) {
        auto hashes = generate_random_hashes(size);

        auto result_original = generate_merkle_root_v0_original(hashes);
        auto result_v1 = generate_merkle_root_v1_inplace(hashes);
        auto result_v2 = generate_merkle_root_v2_binary_counter(hashes);
        auto result_v3 = generate_merkle_root_v3_sha256d64(hashes);
        auto result_v4 = generate_merkle_root_v4_sha256d64_batched(hashes);

        if (result_original != result_v1) {
            fmt::print("ERROR: Mismatch at size {} between original and v1\n", size);
            return false;
        }
        if (result_original != result_v2) {
            fmt::print("ERROR: Mismatch at size {} between original and v2 (binary counter)\n", size);
            return false;
        }
        if (result_original != result_v3) {
            fmt::print("ERROR: Mismatch at size {} between original and v3 (SHA256D64)\n", size);
            return false;
        }
        if (result_original != result_v4) {
            fmt::print("ERROR: Mismatch at size {} between original and v4 (SHA256D64 batched)\n", size);
            return false;
        }
    }

    fmt::print("All implementations produce identical results.\n\n");
    return true;
}

// =============================================================================
// Benchmarks
// =============================================================================

void benchmark_all_sizes() {
    fmt::print("\n========== MERKLE ROOT GENERATION ==========\n");

    // Various block sizes to test
    // From small early blocks to large modern blocks + stress test
    std::vector<size_t> sizes = {1, 2, 5, 10, 50, 100, 250, 500, 1000, 2000, 5000, 10000, 100000};

    for (auto size : sizes) {
        auto hashes = generate_random_hashes(size);

        Bench()
            .title(fmt::format("Merkle Root - {} txs", size))
            .relative(true)
            .run("Original (kth SHA256)", [&] {
                ankerl::nanobench::doNotOptimizeAway(
                    generate_merkle_root_v0_original(hashes)
                );
            })
            .run("Production (SHA256D64 batched)", [&] {
                ankerl::nanobench::doNotOptimizeAway(
                    generate_merkle_root_v4_sha256d64_batched(hashes)
                );
            });
    }
}

// =============================================================================
// Main
// =============================================================================

int main() {
    fmt::print("==============================================\n");
    fmt::print("  Merkle Root Generation Benchmarks\n");
    fmt::print("  Using nanobench\n");
    fmt::print("==============================================\n\n");

    // Initialize Bitcoin Core SHA256 (autodetects CPU features)
    std::string impl = ::kth::SHA256AutoDetect();
    fmt::print("Bitcoin Core SHA256 implementation: {}\n\n", impl);

    // First verify correctness
    if (!verify_implementations()) {
        fmt::print("ERROR: Implementation verification failed!\n");
        return 1;
    }

    benchmark_all_sizes();

    fmt::print("\n==============================================\n");
    fmt::print("  Benchmarks Complete!\n");
    fmt::print("==============================================\n");

    return 0;
}
