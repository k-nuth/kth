// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_DATA_GEN_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_DATA_GEN_HPP

#include "common.hpp"

#include <random>
#include <vector>

// =============================================================================
// Chain generation utilities
// =============================================================================

inline hash_digest generate_hash(std::mt19937& rng) {
    hash_digest h{};
    std::uniform_int_distribution<uint8_t> dist;
    for (auto& byte : h) {
        byte = dist(rng);
    }
    return h;
}

inline header_data generate_header(std::mt19937& rng, hash_digest const& prev_hash) {
    std::uniform_int_distribution<uint32_t> dist32;
    return header_data{
        .version = dist32(rng),
        .prev_block_hash = prev_hash,
        .merkle_root = generate_hash(rng),
        .timestamp = dist32(rng),
        .bits = dist32(rng),
        .nonce = dist32(rng)
    };
}

// Generate a chain of headers (like blockchain)
// This generates a LINEAR chain - each block's parent is the previous one
inline
std::vector<std::pair<hash_digest, header_data>> generate_chain(size_t length, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::vector<std::pair<hash_digest, header_data>> chain;
    chain.reserve(length);

    hash_digest prev_hash{};  // Genesis has zero prev hash

    for (size_t i = 0; i < length; ++i) {
        header_data hdr = generate_header(rng, prev_hash);
        hash_digest hash = generate_hash(rng);
        chain.emplace_back(hash, hdr);
        prev_hash = hash;
    }

    return chain;
}

// Generate a realistic blockchain with occasional orphan blocks
struct realistic_chain_data {
    std::vector<std::pair<hash_digest, header_data>> blocks;  // All blocks in insertion order
    std::vector<size_t> main_chain_indices;                   // Indices of main chain blocks
    size_t tip_index;                                         // Index of the chain tip
};

inline
realistic_chain_data generate_realistic_chain(size_t main_chain_length,
                                               double orphan_rate = 0.001,  // 0.1% orphans
                                               uint32_t seed = 42) {
    std::mt19937 rng(seed);
    realistic_chain_data result;
    result.blocks.reserve(main_chain_length * (1.0 + orphan_rate * 2));
    result.main_chain_indices.reserve(main_chain_length);

    hash_digest prev_hash{};  // Genesis has zero prev hash

    for (size_t i = 0; i < main_chain_length; ++i) {
        // Generate main chain block
        header_data hdr = generate_header(rng, prev_hash);
        hash_digest hash = generate_hash(rng);

        result.main_chain_indices.push_back(result.blocks.size());
        result.blocks.emplace_back(hash, hdr);

        // Occasionally generate an orphan block (competing block at same height)
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (i > 0 && dist(rng) < orphan_rate) {
            // Orphan points to same parent as current block
            header_data orphan_hdr = generate_header(rng, prev_hash);
            hash_digest orphan_hash = generate_hash(rng);
            result.blocks.emplace_back(orphan_hash, orphan_hdr);
            // This orphan is NOT in main_chain_indices
        }

        prev_hash = hash;  // Main chain continues from this block
    }

    result.tip_index = result.main_chain_indices.back();
    return result;
}

// Prevent compiler from optimizing away results
template<typename T>
inline void do_not_optimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_DATA_GEN_HPP
