// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_COMMON_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_COMMON_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// =============================================================================
// Common types (simplified from kth)
// Types are in global namespace for ease of use in benchmark code
// =============================================================================

using hash_digest = std::array<uint8_t, 32>;

constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

struct hash_digest_hasher {
    size_t operator()(hash_digest const& h) const noexcept {
        // Use first 8 bytes as hash (like BCHN's ReadLE64)
        size_t result = 0;
        std::memcpy(&result, h.data(), sizeof(result));
        return result;
    }
};

// Simplified header (80 bytes like real Bitcoin header)
struct header_data {
    uint32_t version;
    hash_digest prev_block_hash;
    hash_digest merkle_root;
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};

// Compact header without prev_block_hash (48 bytes)
// Used by compact implementations where prev_block_hash is redundant
struct header_compact {
    uint32_t version;
    hash_digest merkle_root;  // 32 bytes
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};  // 48 bytes total

// =============================================================================
// Skip list helpers (from BCHN)
// Used for O(log n) ancestor lookup
// =============================================================================

// Invert the lowest set bit (e.g., 6 (110) -> 4 (100), 8 (1000) -> 0)
inline int invert_lowest_one(int n) {
    return n & (n - 1);
}

// Compute what height to jump back to with pskip pointer
// This formula gives O(log n) traversal - max ~110 steps for 2^18 blocks
inline int get_skip_height(int height) {
    if (height < 2) {
        return 0;
    }
    // For odd heights: more aggressive skip
    // For even heights: just invert lowest bit
    return (height & 1) ? invert_lowest_one(invert_lowest_one(height - 1)) + 1
                        : invert_lowest_one(height);
}

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_COMMON_HPP
