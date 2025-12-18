// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <kth/domain.hpp>
#include <fmt/core.h>

#include <random>
#include <vector>

using namespace kth;
using namespace kth::domain::chain;
using ankerl::nanobench::Bench;

// Common benchmark configuration for stability
inline Bench stable_bench() {
    return Bench()
        .warmup(100)      // Warmup iterations to stabilize CPU/caches
        .epochs(20)       // More epochs for better statistics
        .epochIterations(0)  // Let nanobench determine optimal iterations
        .relative(false);
}

#if defined(KTH_USE_HEADER_MEMBERS)
    constexpr char const* implementation_name = "header_members (member-based)";
#else
    constexpr char const* implementation_name = "header_raw (array-based)";
#endif

// Test data generation
struct test_header_data {
    uint32_t version;
    hash_digest previous_block_hash;
    hash_digest merkle;
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};

std::vector<test_header_data> generate_test_headers(size_t count) {
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<uint32_t> dist32;
    std::uniform_int_distribution<uint8_t> dist8;

    std::vector<test_header_data> headers;
    headers.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        test_header_data data;
        data.version = dist32(rng);
        data.timestamp = dist32(rng);
        data.bits = dist32(rng);
        data.nonce = dist32(rng);

        for (auto& byte : data.previous_block_hash) {
            byte = dist8(rng);
        }
        for (auto& byte : data.merkle) {
            byte = dist8(rng);
        }

        headers.push_back(data);
    }

    return headers;
}

// Generate raw 80-byte header data
std::vector<std::array<uint8_t, 80>> generate_raw_headers(size_t count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint8_t> dist8;

    std::vector<std::array<uint8_t, 80>> raw_headers;
    raw_headers.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        std::array<uint8_t, 80> raw{};
        for (auto& byte : raw) {
            byte = dist8(rng);
        }
        raw_headers.push_back(raw);
    }

    return raw_headers;
}

void benchmark_construction() {
    fmt::print("\n========== CONSTRUCTION BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(10000);
    size_t idx = 0;

    // Construction from individual fields
    stable_bench().title("Construction from Fields").minEpochIterations(10000)
        .run(implementation_name, [&] {
            auto const& d = test_data[idx % test_data.size()];
            header h{d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce};
            ankerl::nanobench::doNotOptimizeAway(h);
            ++idx;
        });

#if ! defined(KTH_USE_HEADER_MEMBERS)
    // Construction from raw bytes (array-based only)
    auto raw_data = generate_raw_headers(10000);
    idx = 0;

    stable_bench().title("Construction from Raw Bytes").minEpochIterations(10000)
        .run("header (from 80-byte array)", [&] {
            auto const& raw = raw_data[idx % raw_data.size()];
            header h{raw};
            ankerl::nanobench::doNotOptimizeAway(h);
            ++idx;
        });
#endif

    // Default construction
    stable_bench().title("Default Construction").minEpochIterations(100000)
        .run(implementation_name, [&] {
            header h{};
            ankerl::nanobench::doNotOptimizeAway(h);
        });
}

void benchmark_field_access() {
    fmt::print("\n========== FIELD ACCESS BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(1000);

    // Pre-construct headers
    std::vector<header> headers;
    headers.reserve(test_data.size());

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    size_t idx = 0;

    // Version access
    stable_bench().title("Access: version").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].version();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // Timestamp access
    stable_bench().title("Access: timestamp").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].timestamp();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // Bits access
    stable_bench().title("Access: bits").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].bits();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // Nonce access
    stable_bench().title("Access: nonce").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].nonce();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // Previous block hash access (32 bytes)
    stable_bench().title("Access: previous_block_hash").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].previous_block_hash();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // Merkle root access (32 bytes)
    stable_bench().title("Access: merkle").minEpochIterations(100000)
        .run(implementation_name, [&] {
            auto v = headers[idx % headers.size()].merkle();
            ankerl::nanobench::doNotOptimizeAway(v);
            ++idx;
        });

    idx = 0;

    // All fields access (simulating typical usage)
    stable_bench().title("Access: ALL fields").minEpochIterations(50000)
        .run(implementation_name, [&] {
            auto const& h = headers[idx % headers.size()];
            ankerl::nanobench::doNotOptimizeAway(h.version());
            ankerl::nanobench::doNotOptimizeAway(h.previous_block_hash());
            ankerl::nanobench::doNotOptimizeAway(h.merkle());
            ankerl::nanobench::doNotOptimizeAway(h.timestamp());
            ankerl::nanobench::doNotOptimizeAway(h.bits());
            ankerl::nanobench::doNotOptimizeAway(h.nonce());
            ++idx;
        });
}

void benchmark_serialization() {
    fmt::print("\n========== SERIALIZATION BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(1000);

    // Pre-construct headers
    std::vector<header> headers;

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    size_t idx = 0;

    // Serialization (to_data)
    stable_bench().title("Serialization: to_data(wire=true)").minEpochIterations(10000)
        .run(implementation_name, [&] {
            auto data = headers[idx % headers.size()].to_data(true);
            ankerl::nanobench::doNotOptimizeAway(data);
            ++idx;
        });

#if ! defined(KTH_USE_HEADER_MEMBERS)
    idx = 0;

    // Raw data access (array-based only - zero-copy)
    stable_bench().title("Raw Data Access (zero-copy)").minEpochIterations(100000)
        .run("header::raw_data()", [&] {
            auto span = headers[idx % headers.size()].raw_data();
            ankerl::nanobench::doNotOptimizeAway(span.data());
            ankerl::nanobench::doNotOptimizeAway(span.size());
            ++idx;
        });
#endif
}

void benchmark_deserialization() {
    fmt::print("\n========== DESERIALIZATION BENCHMARKS ==========\n");

    // Generate serialized data
    auto test_data = generate_test_headers(1000);
    std::vector<data_chunk> serialized_data;

    for (auto const& d : test_data) {
        header h{d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce};
        serialized_data.push_back(h.to_data(true));
    }

    size_t idx = 0;

    // Deserialization (from_data)
    stable_bench().title("Deserialization: from_data(wire=true)").minEpochIterations(10000)
        .run(implementation_name, [&] {
            auto const& data = serialized_data[idx % serialized_data.size()];
            byte_reader reader(data);
            auto result = header::from_data(reader, true);
            ankerl::nanobench::doNotOptimizeAway(result);
            ++idx;
        });
}

void benchmark_hashing() {
    fmt::print("\n========== HASHING BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(1000);

    // Pre-construct headers
    std::vector<header> headers;

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    size_t idx = 0;

    // Hash computation
    stable_bench().title("Hash Computation: chain::hash()").minEpochIterations(10000)
        .run(implementation_name, [&] {
            auto h = hash(headers[idx % headers.size()]);
            ankerl::nanobench::doNotOptimizeAway(h);
            ++idx;
        });
}

void benchmark_copy_move() {
    fmt::print("\n========== COPY/MOVE BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(1000);

    // Pre-construct headers
    std::vector<header> headers;

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    size_t idx = 0;

    // Copy construction
    stable_bench().title("Copy Construction").minEpochIterations(50000)
        .run(implementation_name, [&] {
            header copy{headers[idx % headers.size()]};
            ankerl::nanobench::doNotOptimizeAway(copy);
            ++idx;
        });

    idx = 0;

    // Move construction
    stable_bench().title("Move Construction").minEpochIterations(50000)
        .run(implementation_name, [&] {
            header src{headers[idx % headers.size()]};
            header moved{std::move(src)};
            ankerl::nanobench::doNotOptimizeAway(moved);
            ++idx;
        });
}

void benchmark_comparison() {
    fmt::print("\n========== COMPARISON BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(1000);

    // Pre-construct headers
    std::vector<header> headers;

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    size_t idx = 0;

    // Equality comparison
    stable_bench().title("Equality Comparison (==)").minEpochIterations(50000)
        .run(implementation_name, [&] {
            auto const& a = headers[idx % headers.size()];
            auto const& b = headers[(idx + 1) % headers.size()];
            ankerl::nanobench::doNotOptimizeAway(a == b);
            ++idx;
        });
}

void benchmark_memory_layout() {
    fmt::print("\n========== MEMORY LAYOUT INFO ==========\n");

    fmt::print("Implementation: {}\n", implementation_name);
    fmt::print("sizeof(header):      {} bytes\n", sizeof(header));
    fmt::print("alignof(header):     {} bytes\n", alignof(header));
    fmt::print("sizeof(hash_digest): {} bytes\n", sizeof(hash_digest));
    fmt::print("\n");
}

void benchmark_batch_operations() {
    fmt::print("\n========== BATCH OPERATION BENCHMARKS ==========\n");

    auto test_data = generate_test_headers(10000);

    // Batch construction
    stable_bench().title("Batch Construction (10000 headers)").minEpochIterations(10)
        .run(implementation_name, [&] {
            std::vector<header> headers;
            headers.reserve(test_data.size());
            for (auto const& d : test_data) {
                headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
            }
            ankerl::nanobench::doNotOptimizeAway(headers);
        });

#if ! defined(KTH_USE_HEADER_MEMBERS)
    auto raw_data = generate_raw_headers(10000);

    stable_bench().title("Batch Construction from Raw (10000 headers)").minEpochIterations(10)
        .run("header (from raw)", [&] {
            std::vector<header> headers;
            headers.reserve(raw_data.size());
            for (auto const& raw : raw_data) {
                headers.emplace_back(raw);
            }
            ankerl::nanobench::doNotOptimizeAway(headers);
        });
#endif

    // Pre-construct for batch hash
    std::vector<header> headers;

    for (auto const& d : test_data) {
        headers.emplace_back(d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce);
    }

    // Batch hashing
    stable_bench().title("Batch Hashing (10000 headers)").minEpochIterations(5)
        .run(implementation_name, [&] {
            hash_list hashes;
            hashes.reserve(headers.size());
            for (auto const& h : headers) {
                hashes.push_back(hash(h));
            }
            ankerl::nanobench::doNotOptimizeAway(hashes);
        });
}

void benchmark_realistic_ibd() {
    fmt::print("\n========== REALISTIC IBD SIMULATION ==========\n");

    // Simulate receiving 2000 headers (typical batch during IBD)
    auto test_data = generate_test_headers(2000);

    // Simulate: deserialize -> access fields -> hash -> store
    stable_bench().title("IBD Simulation: construct + access + hash (2000 headers)").minEpochIterations(5)
        .run(implementation_name, [&] {
            std::vector<header> headers;
            hash_list hashes;
            headers.reserve(2000);
            hashes.reserve(2000);

            for (auto const& d : test_data) {
                // Construct header
                header h{d.version, d.previous_block_hash, d.merkle, d.timestamp, d.bits, d.nonce};

                // Access fields (typical validation)
                auto v = h.version();
                auto t = h.timestamp();
                auto b = h.bits();
                ankerl::nanobench::doNotOptimizeAway(v);
                ankerl::nanobench::doNotOptimizeAway(t);
                ankerl::nanobench::doNotOptimizeAway(b);

                // Compute hash
                auto h_hash = hash(h);
                hashes.push_back(h_hash);

                // Store header
                headers.push_back(std::move(h));
            }
            ankerl::nanobench::doNotOptimizeAway(headers);
            ankerl::nanobench::doNotOptimizeAway(hashes);
        });
}

int main() {
    fmt::print("==============================================\n");
    fmt::print("  Header Implementation Benchmarks\n");
    fmt::print("  Implementation: {}\n", implementation_name);
    fmt::print("  Using nanobench\n");
    fmt::print("==============================================\n");

    benchmark_memory_layout();
    benchmark_construction();
    benchmark_field_access();
    benchmark_serialization();
    benchmark_deserialization();
    benchmark_hashing();
    benchmark_copy_move();
    benchmark_comparison();
    benchmark_batch_operations();
    benchmark_realistic_ibd();

    fmt::print("\n==============================================\n");
    fmt::print("  Benchmarks Complete!\n");
    fmt::print("==============================================\n");

    return 0;
}

