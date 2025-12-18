// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_03_INDEX_BASED_SOA_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_03_INDEX_BASED_SOA_HPP

#include "common.hpp"

#include <mutex>
#include <shared_mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <fmt/core.h>


// =============================================================================
// Implementation 2c: Index-based with Structure of Arrays (SoA)
// Separates parent_indices from block data for better cache efficiency
//
// Problem with AoS (Array of Structures):
//   blocks_[idx].parent_idx has stride of ~104 bytes (sizeof(block_index))
//   Each cache line (64 bytes) loads ~0.6 blocks, wasting bandwidth
//
// Solution with SoA (Structure of Arrays):
//   parent_indices_[idx] has stride of 4 bytes
//   Each cache line loads 16 parent indices = 16x better cache utilization
// =============================================================================

namespace index_based_soa {

constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

struct block_index {
    // Header data
    header_data hdr{};

    // Height
    int32_t height = 0;

    // Chain work (simplified)
    uint64_t chain_work = 0;

    // Status flags
    uint32_t status = 0;

    // Note: parent_idx is stored in a SEPARATE vector for cache efficiency

    block_index() = default;
    explicit block_index(header_data const& h) : hdr(h) {}
};

class block_index_store {
    mutable std::shared_mutex mutex_;

    // Structure of Arrays: separate vectors for different access patterns
    std::vector<uint32_t> parent_indices_;  // Hot path: 4-byte stride for traversal
    std::vector<uint32_t> skip_indices_;    // Hot path: 4-byte stride for O(log n) ancestor lookup
    std::vector<block_index> blocks_;       // Cold path: full block data

    boost::unordered_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
    // Pre-allocate storage
    void preallocate(size_t n) {
        std::unique_lock lock(mutex_);
        parent_indices_.reserve(n);
        skip_indices_.reserve(n);
        blocks_.reserve(n);
        hash_to_idx_.reserve(n);
    }

    // Build skip index for a block (called during add)
    uint32_t build_skip(uint32_t parent_idx, int32_t height) {
        if (parent_idx == null_index) {
            return null_index;
        }
        return get_ancestor_skip_internal(parent_idx, get_skip_height(height));
    }

    // Internal get_ancestor_skip that doesn't take locks (used during add)
    uint32_t get_ancestor_skip_internal(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }
        if (target_height > blocks_[start_idx].height) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = blocks_[idx].height;

        while (height_walk > target_height) {
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (skip_indices_[idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = skip_indices_[idx];
                height_walk = height_skip;
            } else {
                idx = parent_indices_[idx];
                --height_walk;
            }
        }
        return idx;
    }

    // Add a new block, returns index
    uint32_t add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(mutex_);

        // Check if already exists
        auto it = hash_to_idx_.find(hash);
        if (it != hash_to_idx_.end()) {
            return it->second;
        }

        uint32_t const idx = static_cast<uint32_t>(blocks_.size());

        // Find parent
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_idx = parent_it->second;
            height = blocks_[parent_idx].height + 1;
            chain_work = blocks_[parent_idx].chain_work + 1;
        }

        // Build skip pointer BEFORE any push_back to ensure all vectors are consistent
        uint32_t skip_idx = build_skip(parent_idx, height);

        // SoA: store parent_idx separately from block data (all vectors stay in sync)
        parent_indices_.push_back(parent_idx);
        skip_indices_.push_back(skip_idx);

        blocks_.push_back(block_index{hdr});
        blocks_.back().height = height;
        blocks_.back().chain_work = chain_work;

        hash_to_idx_[hash] = idx;
        return idx;
    }

    // Lookup by hash, returns index
    uint32_t find_idx(hash_digest const& hash) const {
        std::shared_lock lock(mutex_);
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? it->second : null_index;
    }

    // Get block by index (no lock - caller must ensure thread safety)
    block_index const* get_unsafe(uint32_t idx) const {
        return (idx < blocks_.size()) ? &blocks_[idx] : nullptr;
    }

    // Get parent index (no lock)
    uint32_t get_parent_idx_unsafe(uint32_t idx) const {
        return (idx < parent_indices_.size()) ? parent_indices_[idx] : null_index;
    }


    // // Traverse using pointers (like BCHN) - no lock version for benchmarking
    // static int traverse_back_unsafe(block_index* start, int max_steps) {
    //     block_index* walk = start;
    //     int count = 0;
    //     while (walk != nullptr && count < max_steps) {
    //         walk = walk->pprev;
    //         ++count;
    //     }
    //     return count;
    // }

    // Fast parent traversal using SoA layout - HOT PATH v1
    // Only touches parent_indices_ vector (4-byte stride, cache-friendly)
    int traverse_back_by_index_unsafe(uint32_t start_idx, int max_steps) const {
        uint32_t idx = start_idx;
        int count = 0;
        while (idx != null_index && count < max_steps) {
            idx = parent_indices_[idx];  // 4-byte stride! Cache-friendly!
            ++count;
        }
        return count;
    }

    // Fast parent traversal using SoA layout - HOT PATH v2
    // Pointer chase on the index array itself
    int traverse_back_by_ptr_unsafe(uint32_t start_idx, int max_steps) const {
        uint32_t const* walk = &parent_indices_[start_idx];
        int count = 0;
        while (*walk != null_index && count < max_steps) {
            walk = &parent_indices_[*walk];
            ++count;
        }
        return count;
    }

    // With lock version
    int traverse_back_by_index(uint32_t start_idx, int max_steps) const {
        std::shared_lock lock(mutex_);
        return traverse_back_by_index_unsafe(start_idx, max_steps);
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start_idx, n, visitor)
    // Walk back N blocks, executing visitor(block_index const&) on each
    // Used by: GetMedianTimePast (collects timestamps)
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        uint32_t idx = start_idx;
        int count = 0;
        while (idx != null_index && count < max_steps) {
            visitor(blocks_[idx]);
            idx = parent_indices_[idx];
            ++count;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear(start_idx, target_height)
    // Jump to ancestor at specific height - O(n) linear traversal
    // Used by: DAA (height - 144), finalization checks, BIP34
    // Only touches blocks_ once, then only parent_indices_ (cache-friendly!)
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }
        int current_height = blocks_[start_idx].height;
        if (target_height > current_height) {
            return null_index;
        }

        // Calculate steps once, then only touch parent_indices_
        int steps = current_height - target_height;
        uint32_t idx = start_idx;
        while (steps-- > 0 && idx != null_index) {
            idx = parent_indices_[idx];
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip(start_idx, target_height)
    // Jump to ancestor at specific height - O(log n) with skip pointers
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }
        if (target_height > blocks_[start_idx].height) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = blocks_[idx].height;

        while (height_walk > target_height) {
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (skip_indices_[idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = skip_indices_[idx];
                height_walk = height_skip;
            } else {
                idx = parent_indices_[idx];
                --height_walk;
            }
        }
        return idx;
    }

    // Alias for backwards compatibility
    uint32_t get_ancestor(uint32_t start_idx, int target_height) const {
        return get_ancestor_linear(start_idx, target_height);
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor(idx_a, idx_b)
    // Find the last common ancestor of two blocks
    // Used by: reorgs, FindFork, LastCommonAncestor
    // Only touches blocks_ twice at start, then only parent_indices_
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        if (idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        // Read heights once at the start
        int height_a = blocks_[idx_a].height;
        int height_b = blocks_[idx_b].height;

        // Equalize heights - calculate steps, only touch parent_indices_
        while (height_a > height_b) {
            idx_a = parent_indices_[idx_a];
            if (idx_a == null_index) return null_index;
            --height_a;
        }
        while (height_b > height_a) {
            idx_b = parent_indices_[idx_b];
            if (idx_b == null_index) return null_index;
            --height_b;
        }

        // Walk back in parallel until they meet - only touches parent_indices_
        while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
            idx_a = parent_indices_[idx_a];
            idx_b = parent_indices_[idx_b];
        }

        return (idx_a == idx_b) ? idx_a : null_index;
    }

    // Diagnostic: check how contiguous the parent indices are
    void print_contiguity_stats() const {
        if (blocks_.empty()) {
            fmt::print("  Vector is empty\n");
            return;
        }

        size_t contiguous = 0;
        size_t non_contiguous = 0;
        size_t genesis = 0;

        for (size_t i = 0; i < parent_indices_.size(); ++i) {
            uint32_t parent_idx = parent_indices_[i];
            if (parent_idx == null_index) {
                ++genesis;
            } else if (parent_idx == i - 1) {
                ++contiguous;
            } else {
                ++non_contiguous;
                if (non_contiguous <= 10) {
                    fmt::print("  Non-contiguous: parent_indices_[{}] = {} (expected {})\n",
                               i, parent_idx, i - 1);
                }
            }
        }

        double pct = blocks_.size() > 1
            ? (100.0 * contiguous / (blocks_.size() - genesis))
            : 0.0;

        fmt::print("  Total blocks: {}\n", blocks_.size());
        fmt::print("  Genesis/roots: {}\n", genesis);
        fmt::print("  Contiguous (parent_idx == idx-1): {} ({:.2f}%)\n", contiguous, pct);
        fmt::print("  Non-contiguous: {}\n", non_contiguous);
    }

    size_t size() const {
        std::shared_lock lock(mutex_);
        return blocks_.size();
    }

    void clear() {
        std::unique_lock lock(mutex_);
        parent_indices_.clear();
        skip_indices_.clear();
        blocks_.clear();
        hash_to_idx_.clear();
    }
};

}  // namespace index_based_soa

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_03_INDEX_BASED_SOA_HPP
