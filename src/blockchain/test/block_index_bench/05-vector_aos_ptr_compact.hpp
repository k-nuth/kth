// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_05_VECTOR_AOS_PTR_COMPACT_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_05_VECTOR_AOS_PTR_COMPACT_HPP

#include "common.hpp"

#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>



// =============================================================================
// Implementation 2e: Vector + AoS with pointers - COMPACT version
// Removes redundant prev_block_hash from header (it's the parent's hash in map)
// Target: 64 bytes or less per block_index (fits in one cache line)
// =============================================================================

namespace vector_aos_ptr_compact {

// Compact header without prev_block_hash (32 bytes saved!)
struct header_compact {
    uint32_t version;
    hash_digest merkle_root;  // 32 bytes
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};  // 48 bytes total

struct block_index {
    // === Cache line 1 (64 bytes) - HOT data for traversal ===
    block_index* pprev = nullptr;           // 8 bytes
    block_index* pskip = nullptr;           // 8 bytes - for O(log n) ancestor lookup
    int32_t height = 0;                     // 4 bytes
    uint32_t status = 0;                    // 4 bytes
    header_compact hdr{};                   // 48 bytes (was 80)
    // Total: 72 bytes - slightly over one cache line

    block_index() = default;
    explicit block_index(header_data const& h)
        : hdr{h.version, h.merkle_root, h.timestamp, h.bits, h.nonce} {}

    // Build skip pointer - called once when block is added
    void build_skip() {
        if (pprev) {
            pskip = pprev->get_ancestor_skip(get_skip_height(height));
        }
    }

    // O(n) linear traversal - baseline for benchmarking
    block_index* get_ancestor_linear(int target_height) {
        if (target_height < 0 || target_height > height) {
            return nullptr;
        }

        block_index* walk = this;
        while (walk != nullptr && walk->height > target_height) {
            walk = walk->pprev;
        }
        return walk;
    }

    block_index const* get_ancestor_linear(int target_height) const {
        return const_cast<block_index*>(this)->get_ancestor_linear(target_height);
    }

    // Like BCHN's GetAncestor - O(log n) with pskip
    block_index* get_ancestor_skip(int target_height) {
        if (target_height < 0 || target_height > height) {
            return nullptr;
        }

        block_index* walk = this;
        int height_walk = height;

        while (height_walk > target_height) {
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (walk->pskip != nullptr &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                // Use pskip for faster traversal
                walk = walk->pskip;
                height_walk = height_skip;
            } else {
                // Fall back to pprev
                walk = walk->pprev;
                --height_walk;
            }
        }
        return walk;
    }

    block_index const* get_ancestor_skip(int target_height) const {
        return const_cast<block_index*>(this)->get_ancestor_skip(target_height);
    }
};

class block_index_store {
    mutable std::shared_mutex mutex_;
    std::vector<block_index> blocks_;
    boost::unordered_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;
    bool reserved_ = false;

public:
    void preallocate(size_t n) {
        std::unique_lock lock(mutex_);
        blocks_.reserve(n);
        hash_to_idx_.reserve(n);
        reserved_ = true;
    }

    block_index* add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(mutex_);

        auto it = hash_to_idx_.find(hash);
        if (it != hash_to_idx_.end()) {
            return &blocks_[it->second];
        }

        assert(reserved_ && "Must call preallocate() before add()");
        assert(blocks_.size() < blocks_.capacity() && "Vector would reallocate!");

        uint32_t const idx = static_cast<uint32_t>(blocks_.size());

        block_index* parent_ptr = nullptr;
        int32_t height = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_ptr = &blocks_[parent_it->second];
            height = parent_ptr->height + 1;
        }

        blocks_.emplace_back(hdr);
        blocks_.back().pprev = parent_ptr;
        blocks_.back().height = height;
        blocks_.back().build_skip();  // Build skip pointer for O(log n) ancestor lookup

        hash_to_idx_[hash] = idx;
        return &blocks_.back();
    }

    block_index* find(hash_digest const& hash) {
        std::shared_lock lock(mutex_);
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? &blocks_[it->second] : nullptr;
    }

    static int traverse_back_unsafe(block_index* start, int max_steps) {
        block_index* walk = start;
        int count = 0;
        while (walk != nullptr && count < max_steps) {
            walk = walk->pprev;
            ++count;
        }
        return count;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start, n, visitor)
    // Walk back N blocks, executing visitor(block_index const&) on each
    // Used by: GetMedianTimePast (collects timestamps)
    // =========================================================================
    template<typename Visitor>
    static void traverse_back(block_index* start, int max_steps, Visitor&& visitor) {
        block_index* walk = start;
        int count = 0;
        while (walk != nullptr && count < max_steps) {
            visitor(*walk);
            walk = walk->pprev;
            ++count;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear(start, target_height)
    // Jump to ancestor at specific height - O(n) linear traversal
    // Baseline for benchmarking
    // =========================================================================
    static block_index* get_ancestor_linear(block_index* start, int target_height) {
        if (start == nullptr) {
            return nullptr;
        }
        return start->get_ancestor_linear(target_height);
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip(start, target_height)
    // Jump to ancestor at specific height - O(log n) with pskip
    // Used by: DAA (height - 144), finalization checks, BIP34
    // =========================================================================
    static block_index* get_ancestor_skip(block_index* start, int target_height) {
        if (start == nullptr) {
            return nullptr;
        }
        return start->get_ancestor_skip(target_height);
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor(a, b)
    // Find the last common ancestor of two blocks
    // Used by: reorgs, FindFork, LastCommonAncestor
    // =========================================================================
    static block_index* find_common_ancestor(block_index* a, block_index* b) {
        if (a == nullptr || b == nullptr) {
            return nullptr;
        }

        // First, equalize heights
        while (a->height > b->height) {
            a = a->pprev;
            if (a == nullptr) return nullptr;
        }
        while (b->height > a->height) {
            b = b->pprev;
            if (b == nullptr) return nullptr;
        }

        // Walk back in parallel until they meet
        while (a != b && a != nullptr && b != nullptr) {
            a = a->pprev;
            b = b->pprev;
        }

        return (a == b) ? a : nullptr;
    }

    size_t size() const {
        std::shared_lock lock(mutex_);
        return blocks_.size();
    }

    void clear() {
        std::unique_lock lock(mutex_);
        blocks_.clear();
        hash_to_idx_.clear();
        reserved_ = false;
    }
};

}  // namespace vector_aos_ptr_compact

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_05_VECTOR_AOS_PTR_COMPACT_HPP
