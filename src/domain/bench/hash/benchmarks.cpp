// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <fmt/core.h>

#include <random>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>

// Bitcoin Core optimized SHA256
#include <crypto/sha256.h>

// Original kth SHA256 implementation (for benchmark comparison)
extern "C" {
#include "external/sha256.h"
}

using namespace kth;
using ankerl::nanobench::Bench;

// =============================================================================
// Test Data Generation
// =============================================================================

std::vector<uint8_t> generate_random_data(size_t size, uint32_t seed = 12345) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    std::vector<uint8_t> data(size);
    for (auto& byte : data) {
        byte = dist(rng);
    }
    return data;
}

// =============================================================================
// Original kth SHA256 wrappers (for benchmark comparison)
// =============================================================================

hash_digest sha256_hash_original(byte_span data) {
    hash_digest hash;
    SHA256_(data.data(), data.size(), hash.data());
    return hash;
}

hash_digest bitcoin_hash_original(byte_span data) {
    return sha256_hash_original(sha256_hash_original(data));
}

// =============================================================================
// SHA256: Original kth vs Bitcoin Core (CSHA256)
// =============================================================================

void benchmark_sha256_comparison() {
    fmt::print("\n========== SHA256: Original kth vs Bitcoin Core ==========\n");

    std::vector<size_t> sizes = {32, 64, 128, 256, 512, 1024, 4096};

    for (auto size : sizes) {
        auto data = generate_random_data(size);

        std::string label = (size < 1024)
            ? fmt::format("{}B", size)
            : fmt::format("{}KB", size / 1024);

        Bench()
            .title(fmt::format("SHA256 - {}", label))
            .relative(true)
            .minEpochIterations(10000)
            .run("Original kth", [&] {
                ankerl::nanobench::doNotOptimizeAway(
                    sha256_hash_original(byte_span(data.data(), data.size()))
                );
            })
            .run("Bitcoin Core (CSHA256)", [&] {
                unsigned char hash[CSHA256::OUTPUT_SIZE];
                CSHA256().Write(data.data(), data.size()).Finalize(hash);
                ankerl::nanobench::doNotOptimizeAway(hash);
            });
    }
}

// =============================================================================
// Double SHA256 (64 bytes): Original kth vs Bitcoin Core SHA256D64
// This is THE critical operation for merkle trees
// =============================================================================

void benchmark_double_sha256_64() {
    fmt::print("\n========== Double SHA256 (64B) - Merkle Critical ==========\n");

    auto data = generate_random_data(64);
    unsigned char output_btc[32];
    hash_digest output_kth;

    Bench()
        .title("Double SHA256 - 64 bytes")
        .relative(true)
        .minEpochIterations(100000)
        .run("Original kth (2x SHA256)", [&] {
            output_kth = bitcoin_hash_original(byte_span(data.data(), 64));
            ankerl::nanobench::doNotOptimizeAway(output_kth);
        })
        .run("Bitcoin Core (SHA256D64)", [&] {
            SHA256D64(output_btc, data.data(), 1);
            ankerl::nanobench::doNotOptimizeAway(output_btc);
        });
}

// =============================================================================
// Batched Double SHA256: Original kth loop vs Bitcoin Core SHA256D64
// SHA256D64 can use SIMD: 2-way (ARM), 4-way (SSE4.1), 8-way (AVX2)
// =============================================================================

void benchmark_batched_double_sha256() {
    fmt::print("\n========== Batched Double SHA256 ==========\n");

    std::vector<size_t> block_counts = {1, 2, 4, 8, 16, 32, 64, 128};

    for (auto blocks : block_counts) {
        auto input = generate_random_data(blocks * 64);
        std::vector<uint8_t> output(blocks * 32);

        Bench()
            .title(fmt::format("{} x 64B blocks", blocks))
            .relative(true)
            .minEpochIterations(1000)
            .run("Original kth (loop)", [&] {
                for (size_t i = 0; i < blocks; ++i) {
                    auto result = bitcoin_hash_original(byte_span(input.data() + i * 64, 64));
                    std::copy(result.begin(), result.end(), output.data() + i * 32);
                }
                ankerl::nanobench::doNotOptimizeAway(output);
            })
            .run("Bitcoin Core (SHA256D64)", [&] {
                SHA256D64(output.data(), input.data(), blocks);
                ankerl::nanobench::doNotOptimizeAway(output);
            });
    }
}

// =============================================================================
// Main
// =============================================================================

int main() {
    fmt::print("==============================================\n");
    fmt::print("  SHA256 Implementation Comparison\n");
    fmt::print("  kth vs Bitcoin Core\n");
    fmt::print("==============================================\n");

    std::string impl = ::kth::SHA256AutoDetect();
    fmt::print("\nBitcoin Core implementation: {}\n", impl);

    benchmark_sha256_comparison();
    benchmark_double_sha256_64();
    benchmark_batched_double_sha256();

    fmt::print("\n==============================================\n");
    fmt::print("  Benchmarks Complete!\n");
    fmt::print("==============================================\n");

    return 0;
}
