// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_11_SOA_DEQUE_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_11_SOA_DEQUE_HPP

#include "common.hpp"

#include <deque>
#include <shared_mutex>

#include <boost/unordered/unordered_flat_map.hpp>

// =============================================================================
// Implementation: SoA Deque - Every field in its own deque
// Like SoA Fully but using std::deque instead of std::vector
//
// Key difference from std::vector:
//   - std::deque::push_back() NEVER invalidates references to existing elements
//   - This gives us pointer/reference stability like std::unordered_map
//   - Trade-off: slightly worse cache locality (chunks vs contiguous memory)
//
// Each field is stored in a separate deque:
//   - parent_indices_  (4 bytes per block)
//   - heights_         (4 bytes per block)
//   - chain_works_     (8 bytes per block)
//   - statuses_        (4 bytes per block)
//   - versions_        (4 bytes per block)
//   - merkle_roots_    (32 bytes per block)
//   - timestamps_      (4 bytes per block)
//   - bits_            (4 bytes per block)
//   - nonces_          (4 bytes per block)
//
// Benefits:
//   - Reference stability on push_back() (unlike vector)
//   - Still good cache efficiency for sequential access within chunks
//   - Each operation only loads the data it needs
// =============================================================================

namespace soa_deque {

constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

class block_index_store {
    mutable std::shared_mutex mutex_;

    // === Structure of Arrays: each field in its own deque ===

    // Traversal data (hot path)
    std::deque<uint32_t> parent_indices_;   // 4 bytes per block
    std::deque<uint32_t> skip_indices_;     // 4 bytes per block - for O(log n) ancestor lookup

    // Block metadata
    std::deque<int32_t> heights_;           // 4 bytes per block
    std::deque<uint64_t> chain_works_;      // 8 bytes per block
    std::deque<uint32_t> statuses_;         // 4 bytes per block

    // Header fields (without prev_block_hash - redundant with parent_indices_)
    std::deque<uint32_t> versions_;         // 4 bytes per block
    std::deque<hash_digest> merkle_roots_;  // 32 bytes per block
    std::deque<uint32_t> timestamps_;       // 4 bytes per block
    std::deque<uint32_t> bits_;             // 4 bytes per block
    std::deque<uint32_t> nonces_;           // 4 bytes per block

    // Hash lookup
    boost::unordered_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
    // Note: std::deque doesn't have reserve(), but we can reserve the hash map
    void preallocate(size_t n) {
        std::unique_lock lock(mutex_);
        hash_to_idx_.reserve(n);
    }

    // Add a new block, returns index
    uint32_t add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(mutex_);
        return add_internal(hash, hdr);
    }

    // Add without lock - FOR TESTING ONLY to expose race conditions
    uint32_t add_unsafe(hash_digest const& hash, header_data const& hdr) {
        return add_internal(hash, hdr);
    }

private:
    uint32_t add_internal(hash_digest const& hash, header_data const& hdr) {
        // Check if already exists
        auto it = hash_to_idx_.find(hash);
        if (it != hash_to_idx_.end()) {
            return it->second;
        }

        uint32_t const idx = uint32_t(parent_indices_.size());

        // Find parent
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_idx = parent_it->second;
            height = heights_[parent_idx] + 1;
            chain_work = chain_works_[parent_idx] + 1;
        }

        // Build skip pointer BEFORE any push_back to ensure all deques are consistent
        uint32_t skip_idx = build_skip(parent_idx, height);

        // Store each field in its own deque (all deques stay in sync)
        // Note: push_back on deque does NOT invalidate existing references!
        parent_indices_.push_back(parent_idx);
        skip_indices_.push_back(skip_idx);
        heights_.push_back(height);
        chain_works_.push_back(chain_work);
        statuses_.push_back(0);

        // Header fields
        versions_.push_back(hdr.version);
        merkle_roots_.push_back(hdr.merkle_root);
        timestamps_.push_back(hdr.timestamp);
        bits_.push_back(hdr.bits);
        nonces_.push_back(hdr.nonce);

        hash_to_idx_[hash] = idx;
        return idx;
    }

public:

    // Lookup by hash, returns index
    uint32_t find_idx(hash_digest const& hash) const {
        std::shared_lock lock(mutex_);
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? it->second : null_index;
    }

    // Lookup by hash, returns index (no lock)
    uint32_t find_idx_unsafe(hash_digest const& hash) const {
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? it->second : null_index;
    }

    // Accessors (no lock - caller must ensure thread safety for the index)
    // Note: deque references are stable, so reading existing elements is safe
    uint32_t get_parent_idx(uint32_t idx) const {
        return (idx < parent_indices_.size()) ? parent_indices_[idx] : null_index;
    }

    int32_t get_height(uint32_t idx) const {
        return (idx < heights_.size()) ? heights_[idx] : 0;
    }

    uint32_t get_timestamp(uint32_t idx) const {
        return (idx < timestamps_.size()) ? timestamps_[idx] : 0;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start_idx, n, visitor)
    // Walk back N blocks, executing visitor(timestamp) on each
    // Used by: GetMedianTimePast (collects timestamps)
    // Only touches parent_indices_ + timestamps_ deques
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        uint32_t idx = start_idx;
        int count = 0;
        while (idx != null_index && count < max_steps) {
            visitor(timestamps_[idx]);
            idx = parent_indices_[idx];
            ++count;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear(start_idx, target_height)
    // Jump to ancestor at specific height - O(n) linear
    // Used by: DAA (height - 144), finalization checks, BIP34
    // Only touches heights_ once, then only parent_indices_ (cache-friendly!)
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }
        int current_height = heights_[start_idx];
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
    // Only touches skip_indices_ + parent_indices_ + heights_ deques
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }
        if (target_height > heights_[start_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = heights_[idx];

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

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor(idx_a, idx_b)
    // Find the last common ancestor of two blocks
    // Used by: reorgs, FindFork, LastCommonAncestor
    // Only touches heights_ twice at start, then only parent_indices_
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        if (idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        // Read heights once at the start
        int height_a = heights_[idx_a];
        int height_b = heights_[idx_b];

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

    size_t size() const {
        std::shared_lock lock(mutex_);
        return parent_indices_.size();
    }

    void clear() {
        std::unique_lock lock(mutex_);
        parent_indices_.clear();
        skip_indices_.clear();
        heights_.clear();
        chain_works_.clear();
        statuses_.clear();
        versions_.clear();
        merkle_roots_.clear();
        timestamps_.clear();
        bits_.clear();
        nonces_.clear();
        hash_to_idx_.clear();
    }

private:
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
        if (target_height > heights_[start_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = heights_[idx];

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
};

}  // namespace soa_deque

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_11_SOA_DEQUE_HPP
