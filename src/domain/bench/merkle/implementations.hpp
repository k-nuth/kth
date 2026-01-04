// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_BENCH_MERKLE_IMPLEMENTATIONS_HPP
#define KTH_DOMAIN_BENCH_MERKLE_IMPLEMENTATIONS_HPP

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Original kth SHA256 implementation (for benchmark comparison)
extern "C" {
#include "external/sha256.h"
}

namespace kth::bench::merkle {

// =============================================================================
// Original kth SHA256 wrappers (for benchmark comparison)
// =============================================================================

inline
hash_digest sha256_hash_original(byte_span data) {
    hash_digest hash;
    SHA256_(data.data(), data.size(), hash.data());
    return hash;
}

inline
hash_digest bitcoin_hash_original(byte_span data) {
    return sha256_hash_original(sha256_hash_original(data));
}

// =============================================================================
// Implementation 0: Original (baseline) - uses build_chunk per pair
// =============================================================================
// This is the original implementation from block_basis::generate_merkle_root()
// Uses original kth SHA256 (not Bitcoin Core)
// Issues:
// - build_chunk({it[0], it[1]}) allocates a data_chunk per pair
// - Two vectors (merkle, update) with swap pattern
// - Slow SHA256 implementation

inline
hash_digest generate_merkle_root_v0_original(hash_list hashes) {
    if (hashes.empty()) {
        return null_hash;
    }

    hash_list update;
    update.reserve((hashes.size() + 1) / 2);

    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        for (auto it = hashes.begin(); it != hashes.end(); it += 2) {
            update.push_back(bitcoin_hash_original(build_chunk({it[0], it[1]})));
        }

        std::swap(hashes, update);
        update.clear();
    }

    return hashes.front();
}

// =============================================================================
// Implementation 1: In-place with fixed buffer
// =============================================================================
// Same algorithm as v0 but:
// - Uses fixed 64-byte buffer instead of build_chunk() allocation per pair
// - In-place modification instead of two vectors with swap
// - O(n) memory (same as v0, but fewer allocations)

inline
hash_digest generate_merkle_root_v1_inplace(hash_list hashes) {
    if (hashes.empty()) {
        return null_hash;
    }

    // Fixed 64-byte buffer for concatenating two hashes - no allocation per pair
    std::array<uint8_t, 64> buffer;

    while (hashes.size() > 1) {
        // Bitcoin merkle: duplicate last hash if odd count at this level
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        // Combine pairs in-place: write results to front of vector
        size_t j = 0;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            std::copy(hashes[i].begin(), hashes[i].end(), buffer.begin());
            std::copy(hashes[i + 1].begin(), hashes[i + 1].end(), buffer.begin() + 32);
            hashes[j++] = bitcoin_hash(buffer);
        }
        hashes.resize(j);
    }

    return hashes.front();
}

// =============================================================================
// Implementation 2: Binary Counter with merkle-specific reduce
// =============================================================================
// Uses Stepanov's binary counter algorithm with a specialized reduce
// that handles Bitcoin merkle's odd-duplication at each level.
// - O(log n) space for the counter (vs O(n) for v0/v1)
// - Single pass through input hashes
// - No vector allocations during processing

#include <kth/domain/algorithm/binary_counter.hpp>

inline
hash_digest generate_merkle_root_v2_binary_counter(hash_list const& hashes) {
    if (hashes.empty()) {
        return null_hash;
    }

    // Operation: concatenate two hashes and compute bitcoin_hash
    auto hash_pair = [](hash_digest const& a, hash_digest const& b) -> hash_digest {
        std::array<uint8_t, 64> buffer;
        std::copy(a.begin(), a.end(), buffer.begin());
        std::copy(b.begin(), b.end(), buffer.begin() + 32);
        return bitcoin_hash(buffer);
    };

    return kth::domain::algorithm::reduce_merkle(
        hashes.begin(),
        hashes.end(),
        hash_pair,
        null_hash
    );
}

// =============================================================================
// Implementation 3: In-place with Bitcoin Core SHA256D64
// =============================================================================
// Uses Bitcoin Core's optimized SHA256D64 which:
// - Uses ARM crypto instructions on Apple Silicon
// - Processes double-SHA256 of 64 bytes in optimized path
// - Can batch multiple pairs with Transform_2way

#include <crypto/sha256.h>

inline
hash_digest generate_merkle_root_v3_sha256d64(hash_list hashes) {
    if (hashes.empty()) {
        return null_hash;
    }

    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        size_t j = 0;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            // Concatenate two hashes into 64-byte buffer
            std::array<uint8_t, 64> buffer;
            std::copy(hashes[i].begin(), hashes[i].end(), buffer.begin());
            std::copy(hashes[i + 1].begin(), hashes[i + 1].end(), buffer.begin() + 32);

            // Use SHA256D64 for double-SHA256
            SHA256D64(hashes[j].data(), buffer.data(), 1);
            ++j;
        }
        hashes.resize(j);
    }

    return hashes.front();
}

// =============================================================================
// Implementation 4: Batched SHA256D64 (process multiple pairs at once)
// =============================================================================
// Batches pairs for SHA256D64 which can use SIMD (2-way on ARM, 4/8-way on x86)

inline
hash_digest generate_merkle_root_v4_sha256d64_batched(hash_list hashes) {
    if (hashes.empty()) {
        return null_hash;
    }

    // Temporary buffer for batching input pairs
    std::vector<uint8_t> batch_input;
    std::vector<uint8_t> batch_output;

    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        size_t num_pairs = hashes.size() / 2;

        // Prepare batch input: all pairs concatenated
        batch_input.resize(num_pairs * 64);
        batch_output.resize(num_pairs * 32);

        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(hashes[i * 2].begin(), hashes[i * 2].end(),
                      batch_input.begin() + i * 64);
            std::copy(hashes[i * 2 + 1].begin(), hashes[i * 2 + 1].end(),
                      batch_input.begin() + i * 64 + 32);
        }

        // Process all pairs at once - SHA256D64 uses SIMD internally
        SHA256D64(batch_output.data(), batch_input.data(), num_pairs);

        // Copy results back
        hashes.resize(num_pairs);
        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(batch_output.begin() + i * 32,
                      batch_output.begin() + (i + 1) * 32,
                      hashes[i].begin());
        }
    }

    return hashes.front();
}

} // namespace kth::bench::merkle

#endif // KTH_DOMAIN_BENCH_MERKLE_IMPLEMENTATIONS_HPP
