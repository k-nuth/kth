// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_IMPL_BCHN_STYLE_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_IMPL_BCHN_STYLE_HPP

#include "common.hpp"

#include <shared_mutex>
#include <unordered_map>

// =============================================================================
// Implementation 1: BCHN-style (std::unordered_map + mutex)
// Pointer stability guaranteed by std::unordered_map
//
// API follows BCHN pattern:
// - Caller must hold cs_main (mutex) BEFORE calling any function
// - Caller must KEEP holding mutex while using returned pointers
// - Functions assert lock is held (like BCHN's AssertLockHeld)
// =============================================================================

namespace bchn_style {

struct block_index {
    // Pointer to hash (points to key in map) - like BCHN
    hash_digest const* phash_block = nullptr;

    // Pointer to parent - relies on pointer stability
    block_index* pprev = nullptr;

    // Skip list pointer for fast ancestor lookup
    block_index* pskip = nullptr;

    // Height
    int32_t height = 0;

    // Header data
    header_data hdr{};

    // Chain work (simplified)
    uint64_t chain_work = 0;

    // Status flags (modified after insertion, like BCHN's nStatus)
    uint32_t status = 0;

    block_index() = default;
    explicit block_index(header_data const& h) : hdr(h) {}

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

    // Build skip pointer - called once when block is added
    void build_skip() {
        if (pprev) {
            pskip = pprev->get_ancestor_skip(get_skip_height(height));
        }
    }
};

// =========================================================================
// PRIMITIVE 1: traverse_back(start, n, visitor)
// Walk back N blocks, executing visitor(block_index const&) on each
// Used by: GetMedianTimePast (collects timestamps)
// =========================================================================
template<typename Visitor>
void traverse_back(block_index* start, int max_steps, Visitor&& visitor) {
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
inline block_index* get_ancestor_linear(block_index* start, int target_height) {
    if (start == nullptr) return nullptr;
    return start->get_ancestor_linear(target_height);
}

// =========================================================================
// PRIMITIVE 2b: get_ancestor_skip(start, target_height)
// Jump to ancestor at specific height - O(log n) with pskip
// Used by: DAA (height - 144), finalization checks, BIP34
// =========================================================================
inline block_index* get_ancestor_skip(block_index* start, int target_height) {
    if (start == nullptr) return nullptr;
    return start->get_ancestor_skip(target_height);
}

// =========================================================================
// PRIMITIVE 3: find_common_ancestor(a, b)
// Find the last common ancestor of two blocks
// Used by: reorgs, FindFork, LastCommonAncestor
// =========================================================================
inline block_index* find_common_ancestor(block_index* a, block_index* b) {
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

// Like BCHN's cs_main - the global mutex for block index access
// In BCHN this is a RecursiveMutex, we use shared_mutex for reader/writer separation
class block_index_store {
public:
    using mutex_type = std::shared_mutex;

private:
    mutable mutex_type cs_main_;  // Named like BCHN
    std::unordered_map<hash_digest, block_index, hash_digest_hasher> map_;

public:
    // Expose mutex for callers to lock (like BCHN exposes cs_main)
    mutex_type& cs_main() { return cs_main_; }
    mutex_type const& cs_main() const { return cs_main_; }

    // ==========================================================================
    // Functions that REQUIRE caller to hold cs_main (like BCHN)
    // ==========================================================================

    // Like BCHN's AddToBlockIndex - requires cs_main held
    block_index* add_to_block_index(hash_digest const& hash, header_data const& hdr) {
        // In real code: AssertLockHeld(cs_main_);

        auto [it, inserted] = map_.try_emplace(hash, hdr);
        if (!inserted) {
            return &it->second;  // Already exists
        }

        block_index* idx = &it->second;
        idx->phash_block = &it->first;

        // Find parent
        auto parent_it = map_.find(hdr.prev_block_hash);
        if (parent_it != map_.end()) {
            idx->pprev = &parent_it->second;
            idx->height = idx->pprev->height + 1;
            idx->chain_work = idx->pprev->chain_work + 1;  // Simplified
            idx->build_skip();  // Build skip pointer for O(log n) ancestor lookup
        }

        return idx;
    }

    // Like BCHN's LookupBlockIndex - requires cs_main held
    block_index* lookup_block_index(hash_digest const& hash) {
        // In real code: AssertLockHeld(cs_main_);
        auto it = map_.find(hash);
        return (it != map_.end()) ? &it->second : nullptr;
    }

    block_index const* lookup_block_index(hash_digest const& hash) const {
        // In real code: AssertLockHeld(cs_main_);
        auto it = map_.find(hash);
        return (it != map_.end()) ? &it->second : nullptr;
    }

    // Size - requires cs_main held
    size_t size() const {
        // In real code: AssertLockHeld(cs_main_);
        return map_.size();
    }

    // Clear - requires cs_main held
    void clear() {
        // In real code: AssertLockHeld(cs_main_);
        map_.clear();
    }

    // ==========================================================================
    // Convenience wrappers (acquire lock internally) - for simple cases
    // WARNING: Only use when you don't need to use the returned pointer!
    // ==========================================================================

    block_index* add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(cs_main_);
        return add_to_block_index(hash, hdr);
    }

    // Add without lock - FOR TESTING ONLY to expose potential race conditions
    block_index* add_unsafe(hash_digest const& hash, header_data const& hdr) {
        return add_to_block_index(hash, hdr);
    }

    block_index* find(hash_digest const& hash) {
        std::shared_lock lock(cs_main_);
        return lookup_block_index(hash);
    }

    // Find without lock - FOR TESTING ONLY
    block_index* find_unsafe(hash_digest const& hash) {
        return lookup_block_index(hash);
    }
};

}  // namespace bchn_style

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_IMPL_BCHN_STYLE_HPP
