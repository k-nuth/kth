// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_16_TBB_CFM_SIMPLE_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_16_TBB_CFM_SIMPLE_HPP

#include "common.hpp"

#include <boost/unordered/concurrent_flat_map.hpp>
#include <tbb/concurrent_vector.h>

// =============================================================================
// Implementation: TBB concurrent_vector + CFM (simple insert)
//
// Simpler version of 15: instead of using insert_and_visit, we:
//   1. First insert into concurrent_vector (get index)
//   2. Then insert into CFM with that index
//
// This is simpler and shows that insert_and_visit doesn't provide
// additional synchronization benefits for our use case.
//
// Trade-off: Using AoS (struct per block) instead of SoA since we can only
// have one concurrent_vector. May have worse cache locality for traversals.
// =============================================================================

namespace tbb_cfm_simple {

static constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

// Block entry - all data for one block (AoS approach)
struct BlockEntry {
    // Traversal data
    uint32_t parent_idx;
    uint32_t skip_idx;

    // Block metadata
    int32_t height;
    uint64_t chain_work;
    uint32_t status;

    // Header fields
    uint32_t version;
    hash_digest merkle_root;
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};

class block_index_store {
    // Thread-safe vector for block storage
    tbb::concurrent_vector<BlockEntry> blocks_;

    // Lock-free hash map
    boost::concurrent_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
    block_index_store() = default;
    ~block_index_store() = default;

    block_index_store(block_index_store const&) = delete;
    block_index_store& operator=(block_index_store const&) = delete;
    block_index_store(block_index_store&&) = delete;
    block_index_store& operator=(block_index_store&&) = delete;

    // Add a new block
    uint32_t add(hash_digest const& hash, header_data const& hdr) {
        // Check if already exists
        uint32_t existing_idx = null_index;
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            existing_idx = pair.second;
        });
        if (existing_idx != null_index) {
            return existing_idx;
        }

        // Find parent info
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        hash_to_idx_.visit(hdr.prev_block_hash, [&](auto const& pair) {
            parent_idx = pair.second;
        });

        if (parent_idx != null_index) {
            BlockEntry const& parent = blocks_[parent_idx];
            height = parent.height + 1;
            chain_work = parent.chain_work + 1;
        }

        // Build skip pointer
        uint32_t skip_idx = build_skip(parent_idx, height);

        // Prepare the block entry
        BlockEntry entry{
            .parent_idx = parent_idx,
            .skip_idx = skip_idx,
            .height = height,
            .chain_work = chain_work,
            .status = 0,
            .version = hdr.version,
            .merkle_root = hdr.merkle_root,
            .timestamp = hdr.timestamp,
            .bits = hdr.bits,
            .nonce = hdr.nonce
        };

        // 1. First: insert into vector, get index
        auto it = blocks_.push_back(entry);
        uint32_t idx = uint32_t(it - blocks_.begin());

        // 2. Then: insert into map
        hash_to_idx_.insert({hash, idx});

        return idx;
    }

    // =========================================================================
    // Lock-free lookup by hash
    // =========================================================================
    uint32_t find_idx(hash_digest const& hash) const {
        uint32_t result = null_index;
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            result = pair.second;
        });
        return result;
    }

    // =========================================================================
    // Lock-free accessors
    // =========================================================================
    uint32_t get_parent_idx(uint32_t idx) const {
        if (idx >= blocks_.size()) return null_index;
        return blocks_[idx].parent_idx;
    }

    int32_t get_height(uint32_t idx) const {
        if (idx >= blocks_.size()) return 0;
        return blocks_[idx].height;
    }

    uint32_t get_timestamp(uint32_t idx) const {
        if (idx >= blocks_.size()) return 0;
        return blocks_[idx].timestamp;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        if (start_idx >= blocks_.size() || max_steps <= 0) {
            return;
        }

        uint32_t idx = start_idx;
        int remaining = max_steps;

        while (idx != null_index && remaining > 0) {
            BlockEntry const& block = blocks_[idx];
            visitor(block.timestamp);
            idx = block.parent_idx;
            --remaining;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx >= blocks_.size() || start_idx == null_index || target_height < 0) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int current_height = blocks_[idx].height;

        if (target_height > current_height) {
            return null_index;
        }

        while (current_height > target_height && idx != null_index) {
            idx = blocks_[idx].parent_idx;
            --current_height;
        }

        return idx;
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx >= blocks_.size() || start_idx == null_index || target_height < 0) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = blocks_[idx].height;

        if (target_height > height_walk) {
            return null_index;
        }

        while (height_walk > target_height) {
            BlockEntry const& block = blocks_[idx];
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (block.skip_idx != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = block.skip_idx;
                height_walk = height_skip;
            } else {
                idx = block.parent_idx;
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
        if (idx_a >= blocks_.size() || idx_b >= blocks_.size() ||
            idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        int height_a = blocks_[idx_a].height;
        int height_b = blocks_[idx_b].height;

        // Equalize heights
        while (height_a > height_b) {
            idx_a = blocks_[idx_a].parent_idx;
            if (idx_a == null_index) return null_index;
            --height_a;
        }

        while (height_b > height_a) {
            idx_b = blocks_[idx_b].parent_idx;
            if (idx_b == null_index) return null_index;
            --height_b;
        }

        // Walk back until they meet
        while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
            idx_a = blocks_[idx_a].parent_idx;
            idx_b = blocks_[idx_b].parent_idx;
        }

        return (idx_a == idx_b) ? idx_a : null_index;
    }

    size_t size() const {
        return blocks_.size();
    }

private:
    uint32_t build_skip(uint32_t parent_idx, int32_t height) {
        if (parent_idx == null_index) {
            return null_index;
        }
        return get_ancestor_skip_for_build(parent_idx, get_skip_height(height));
    }

    // Version used during build - accesses blocks_ directly
    uint32_t get_ancestor_skip_for_build(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = blocks_[idx].height;

        if (target_height > height_walk) {
            return null_index;
        }

        while (height_walk > target_height) {
            BlockEntry const& block = blocks_[idx];
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (block.skip_idx != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = block.skip_idx;
                height_walk = height_skip;
            } else {
                idx = block.parent_idx;
                --height_walk;
            }
        }
        return idx;
    }
};

} // namespace tbb_cfm_simple

#endif // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_16_TBB_CFM_SIMPLE_HPP
