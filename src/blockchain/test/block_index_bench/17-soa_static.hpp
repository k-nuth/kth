// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_17_SOA_STATIC_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_17_SOA_STATIC_HPP

#include "common.hpp"

#include <atomic>
#include <tuple>
#include <vector>

#include <boost/unordered/concurrent_flat_map.hpp>

// =============================================================================
// Implementation 17: SoA Static Array
//
// Based on implementation 14 (soa_chunks_cfm_sync), but simplified:
//   - NO chunks! Single static array for each field
//   - Capacity determined at compile-time via KTH_BLOCK_INDEX_capacity
//   - Returns warning flag when capacity reaches 95%
//
// Memory layout (for ~1.18M blocks, default capacity):
//   parent_indices:   4.7 MB (1,179,648 × 4 bytes)
//   skip_indices:     4.7 MB
//   heights:          4.7 MB
//   chain_works:      9.4 MB (1,179,648 × 8 bytes)
//   statuses:         4.7 MB
//   versions:         4.7 MB
//   merkle_roots:    37.7 MB (1,179,648 × 32 bytes)
//   timestamps:       4.7 MB
//   bits:             4.7 MB
//   nonces:           4.7 MB
//   ─────────────────────────
//   Total:          ~85 MB
//
// Thread safety (same as impl 14):
//   - Writers: CFM's bucket lock during insert_and_visit
//   - Readers: CFM's bucket lock during visit/cvisit provides acquire semantics
//   - Traversal: safe via happens-before transitivity through ancestor chain
// =============================================================================

namespace soa_static {

// Default capacity from CMake/Conan, with fallback default (~2030, aligned to 2^16)
#ifndef KTH_BLOCK_INDEX_capacity
#define KTH_BLOCK_INDEX_capacity 1179648
#endif

static constexpr size_t default_capacity = KTH_BLOCK_INDEX_capacity;
static constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

class block_index_store {
    // SoA: one vector per field (pre-allocated to capacity)
    std::vector<uint32_t> parent_indices_;
    std::vector<uint32_t> skip_indices_;
    std::vector<int32_t> heights_;
    std::vector<uint64_t> chain_works_;
    std::vector<uint32_t> statuses_;
    std::vector<uint32_t> versions_;
    std::vector<hash_digest> merkle_roots_;
    std::vector<uint32_t> timestamps_;
    std::vector<uint32_t> bits_;
    std::vector<uint32_t> nonces_;

    // Index counter
    std::atomic<uint32_t> next_idx_{0};

    // Lock-free hash map - provides all synchronization!
    boost::concurrent_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

    // Runtime capacity
    size_t capacity_;
    size_t warn_threshold_;

public:
    explicit block_index_store(size_t cap = default_capacity)
        : capacity_(cap)
        , warn_threshold_((cap * 95) / 100)
    {
        // Pre-allocate all vectors to capacity
        parent_indices_.resize(capacity_);
        skip_indices_.resize(capacity_);
        heights_.resize(capacity_);
        chain_works_.resize(capacity_);
        statuses_.resize(capacity_);
        versions_.resize(capacity_);
        merkle_roots_.resize(capacity_);
        timestamps_.resize(capacity_);
        bits_.resize(capacity_);
        nonces_.resize(capacity_);
        hash_to_idx_.reserve(capacity_);
    }

    ~block_index_store() = default;

    block_index_store(block_index_store const&) = delete;
    block_index_store& operator=(block_index_store const&) = delete;
    block_index_store(block_index_store&&) = delete;
    block_index_store& operator=(block_index_store&&) = delete;


    // =========================================================================
    // ADD - Uses CFM's insert_and_visit for synchronization
    // Returns: {inserted, index, warn}
    //   - inserted: true if new block was added
    //   - index: the block's index
    //   - warn: true if we're at 95% capacity (time to upgrade!)
    // =========================================================================
    std::tuple<bool, uint32_t, bool> add(hash_digest const& hash, header_data const& hdr) {
        uint32_t result_idx = null_index;
        bool is_inserted = false;

        // IMPORTANT: Find parent BEFORE insert_and_visit to avoid nested visits
        // (nested visits on same bucket = deadlock!)
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        hash_to_idx_.visit(hdr.prev_block_hash, [&](auto const& parent_pair) {
            parent_idx = parent_pair.second;
        });

        if (parent_idx != null_index) {
            height = heights_[parent_idx] + 1;
            chain_work = chain_works_[parent_idx] + 1;
        }

        // Now do the insert with parent info already available
        hash_to_idx_.insert_and_visit(
            {hash, null_index},
            [&](auto& pair) {
                // Newly inserted - initialize everything
                uint32_t const idx = next_idx_.fetch_add(1, std::memory_order_relaxed);

                // Bounds check (should never happen if capacity is sized correctly)
                if (idx >= capacity_) {
                    // This is a fatal error - we've exceeded capacity
                    // In production, this should trigger an alert/shutdown
                    result_idx = null_index;
                    return;
                }

                uint32_t const skip_idx = build_skip(parent_idx, height);

                // Store in vectors (all writes happen under bucket lock)
                parent_indices_[idx] = parent_idx;
                skip_indices_[idx] = skip_idx;
                heights_[idx] = height;
                chain_works_[idx] = chain_work;
                statuses_[idx] = 0;
                versions_[idx] = hdr.version;
                merkle_roots_[idx] = hdr.merkle_root;
                timestamps_[idx] = hdr.timestamp;
                bits_[idx] = hdr.bits;
                nonces_[idx] = hdr.nonce;

                // Update the map value with actual index
                pair.second = idx;
                result_idx = idx;
                is_inserted = true;
            },
            [&](auto const& pair) {
                // Already existed - just get the index
                result_idx = pair.second;
            }
        );

        bool const warn = (result_idx != null_index && result_idx >= warn_threshold_);
        return {is_inserted, result_idx, warn};
    }

    // =========================================================================
    // FIND - Lock-free lookup via CFM
    // =========================================================================
    uint32_t find_idx(hash_digest const& hash) const {
        uint32_t result = null_index;
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            result = pair.second;
        });
        return result;
    }

    // =========================================================================
    // Accessors
    // =========================================================================
    int32_t get_height(uint32_t idx) const {
        return heights_[idx];
    }

    uint32_t get_timestamp(uint32_t idx) const {
        return timestamps_[idx];
    }

    uint32_t get_parent_idx(uint32_t idx) const {
        return parent_indices_[idx];
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        if (start_idx == null_index || max_steps <= 0) {
            return;
        }

        uint32_t idx = start_idx;
        int remaining = max_steps;

        while (idx != null_index && remaining > 0) {
            visitor(timestamps_[idx]);
            idx = parent_indices_[idx];
            --remaining;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        int current_height = heights_[start_idx];
        if (target_height > current_height) {
            return null_index;
        }

        int remaining = current_height - target_height;
        uint32_t idx = start_idx;

        while (remaining > 0 && idx != null_index) {
            idx = parent_indices_[idx];
            --remaining;
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        if (target_height > heights_[start_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = heights_[start_idx];

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

            if (height_walk <= target_height) {
                break;
            }
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        if (idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        int height_a = heights_[idx_a];
        int height_b = heights_[idx_b];

        // Equalize heights
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

        // Walk back in parallel until they meet
        while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
            idx_a = parent_indices_[idx_a];
            idx_b = parent_indices_[idx_b];
        }

        return (idx_a == idx_b) ? idx_a : null_index;
    }

    size_t size() const {
        return hash_to_idx_.size();
    }

private:
    uint32_t build_skip(uint32_t parent_idx, int32_t height) {
        if (parent_idx == null_index) {
            return null_index;
        }
        return get_ancestor_skip_internal(parent_idx, get_skip_height(height));
    }

    // Internal version: called during add() under CFM's bucket lock
    uint32_t get_ancestor_skip_internal(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        if (target_height > heights_[start_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = heights_[start_idx];

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

            if (height_walk <= target_height) {
                break;
            }
        }
        return idx;
    }
};

}  // namespace soa_static

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_17_SOA_STATIC_HPP
