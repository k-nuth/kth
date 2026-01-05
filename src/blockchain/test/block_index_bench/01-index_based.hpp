// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_01_INDEX_BASED_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_01_INDEX_BASED_HPP

#include "common.hpp"

#include <shared_mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

// =============================================================================
// Implementation 2: Index-based (unordered_flat_map + vector)
// Uses indices instead of pointers - no pointer stability needed
// =============================================================================

namespace index_based {

constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

struct block_index {
// Header data
header_data hdr{};

// Parent index (not pointer)
uint32_t parent_idx = null_index;

// Skip list index
uint32_t skip_idx = null_index;

// Height
int32_t height = 0;

// Chain work (simplified)
uint64_t chain_work = 0;

// Status flags
uint32_t status = 0;

block_index() = default;
explicit block_index(header_data const& h) : hdr(h) {}
};

class block_index_store {
mutable std::shared_mutex mutex_;
std::vector<block_index> blocks_;
boost::unordered_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
// Add a new block, returns index
uint32_t add(hash_digest const& hash, header_data const& hdr) {
    std::unique_lock lock(mutex_);

    // Check if already exists
    auto it = hash_to_idx_.find(hash);
    if (it != hash_to_idx_.end()) {
        return it->second;
    }

    uint32_t const idx = uint32_t(blocks_.size());

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

    blocks_.push_back(block_index{hdr});
    blocks_.back().parent_idx = parent_idx;
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

// Get block by index
block_index const* get(uint32_t idx) const {
    std::shared_lock lock(mutex_);
    return (idx < blocks_.size()) ? &blocks_[idx] : nullptr;
}

// Get parent
block_index const* parent(block_index const& b) const {
    std::shared_lock lock(mutex_);
    return (b.parent_idx != null_index) ? &blocks_[b.parent_idx] : nullptr;
}

size_t size() const {
    std::shared_lock lock(mutex_);
    return blocks_.size();
}

void clear() {
    std::unique_lock lock(mutex_);
    blocks_.clear();
    hash_to_idx_.clear();
}

void reserve(size_t n) {
    std::unique_lock lock(mutex_);
    blocks_.reserve(n);
    hash_to_idx_.reserve(n);
}
};

}  // namespace index_based

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_01_INDEX_BASED_HPP
