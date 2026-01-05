// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_07_VECTOR_AOS_PTR_64_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_07_VECTOR_AOS_PTR_64_HPP

#include "common.hpp"

#include <cassert>
#include <shared_mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>


// =============================================================================
// Implementation 2g: Vector + AoS + Ptr + 64 bytes (no pskip)
// Fits exactly in one x86-64 cache line (64 bytes)
// Trade-off: GetAncestor becomes O(n) instead of O(log n)
// =============================================================================

namespace vector_aos_ptr_64 {

using header_compact = vector_aos_ptr_compact::header_compact;  // 48 bytes

struct block_index {
    block_index* pprev = nullptr;           // 8 bytes
    int32_t height = 0;                     // 4 bytes
    uint32_t status = 0;                    // 4 bytes
    header_compact hdr{};                   // 48 bytes
    // Total: 64 bytes - exactly one cache line on x86-64
};

static_assert(sizeof(block_index) == 64, "block_index must be exactly 64 bytes");

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

        uint32_t const idx = uint32_t(blocks_.size());

        block_index* parent_ptr = nullptr;
        int32_t height = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_ptr = &blocks_[parent_it->second];
            height = parent_ptr->height + 1;
        }

        blocks_.emplace_back();
        blocks_.back().pprev = parent_ptr;
        blocks_.back().height = height;
        blocks_.back().hdr = {hdr.version, hdr.merkle_root, hdr.timestamp, hdr.bits, hdr.nonce};

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
    // PRIMITIVE 2: get_ancestor_linear(start, target_height)
    // Jump to ancestor at specific height - O(n) without pskip
    // Used by: DAA (height - 144), finalization checks, BIP34
    // =========================================================================
    static block_index* get_ancestor_linear(block_index* start, int target_height) {
        if (start == nullptr || target_height < 0) {
            return nullptr;
        }
        if (target_height > start->height) {
            return nullptr;
        }

        block_index* walk = start;
        while (walk != nullptr && walk->height > target_height) {
            walk = walk->pprev;
        }
        return (walk != nullptr && walk->height == target_height) ? walk : nullptr;
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

}  // namespace vector_aos_ptr_64

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_07_VECTOR_AOS_PTR_64_HPP
