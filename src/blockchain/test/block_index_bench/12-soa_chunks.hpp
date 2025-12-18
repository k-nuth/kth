// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_12_SOA_CHUNKS_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_12_SOA_CHUNKS_HPP

#include "common.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <shared_mutex>

#include <boost/unordered/unordered_flat_map.hpp>

// =============================================================================
// Implementation: SoA Chunks - Lock-free reads with chunked storage
//
// Design goals:
//   - Lock-free traversal (like BCHN pointer-based)
//   - Cache-efficient SoA layout within each chunk
//   - Lazy allocation: only allocate chunks as needed
//   - No reallocation: chunks never move once allocated
//
// Memory layout:
//   - Directory: fixed array of MAX_CHUNKS atomic pointers (~80 bytes)
//   - Each Chunk: pre-allocated arrays for CHUNK_SIZE elements (~72MB per chunk)
//   - Total capacity: MAX_CHUNKS * CHUNK_SIZE elements (10M with defaults)
//
// Thread safety:
//   - Writers: must hold mutex for insert (serialized)
//   - Readers: lock-free traversal using atomic size_ and chunk pointers
//   - Chunk allocation: done under mutex, visible to readers via atomic store
// =============================================================================

namespace soa_chunks {

// Configuration - CHUNK_SIZE must be power of 2 for fast division
static constexpr size_t CHUNK_BITS = 20;                        // 2^20 = 1,048,576
static constexpr size_t CHUNK_SIZE = size_t{1} << CHUNK_BITS;   // ~1M elements per chunk
static constexpr size_t CHUNK_MASK = CHUNK_SIZE - 1;
static constexpr size_t MAX_CHUNKS = 10;                        // ~10M blocks max (~100 years of BCH)

static constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

// A chunk contains all SoA arrays for CHUNK_SIZE elements
// Pre-allocated entirely when created (~72MB per chunk)
struct Chunk {
    // Traversal data (hot path)
    std::array<uint32_t, CHUNK_SIZE> parent_indices;
    std::array<uint32_t, CHUNK_SIZE> skip_indices;

    // Block metadata
    std::array<int32_t, CHUNK_SIZE> heights;
    std::array<uint64_t, CHUNK_SIZE> chain_works;
    std::array<uint32_t, CHUNK_SIZE> statuses;

    // Header fields
    std::array<uint32_t, CHUNK_SIZE> versions;
    std::array<hash_digest, CHUNK_SIZE> merkle_roots;
    std::array<uint32_t, CHUNK_SIZE> timestamps;
    std::array<uint32_t, CHUNK_SIZE> bits;
    std::array<uint32_t, CHUNK_SIZE> nonces;
};

class block_index_store {
    // Directory: fixed array of chunk pointers (never reallocates)
    std::array<std::atomic<Chunk*>, MAX_CHUNKS> chunks_{};

    // Current size (total elements across all chunks)
    std::atomic<uint32_t> size_{0};

    // Mutex for writers (insert serialization and chunk allocation)
    mutable std::shared_mutex mutex_;

    // Hash lookup (still needs locking for now)
    boost::unordered_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
    block_index_store() {
        // Initialize all chunk pointers to nullptr
        for (auto& chunk : chunks_) {
            chunk.store(nullptr, std::memory_order_relaxed);
        }
    }

    ~block_index_store() {
        // Clean up allocated chunks
        for (auto& chunk : chunks_) {
            Chunk* ptr = chunk.load(std::memory_order_relaxed);
            delete ptr;
        }
    }

    // Non-copyable, non-movable (due to atomics)
    block_index_store(block_index_store const&) = delete;
    block_index_store& operator=(block_index_store const&) = delete;
    block_index_store(block_index_store&&) = delete;
    block_index_store& operator=(block_index_store&&) = delete;

    // Add a new block, returns index (thread-safe, serialized)
    uint32_t add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(mutex_);
        return add_internal(hash, hdr);
    }

    // Add without lock - FOR TESTING ONLY
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

        uint32_t sz = size_.load(std::memory_order_relaxed);
        uint32_t chunk_idx = sz >> CHUNK_BITS;
        uint32_t local_idx = sz & CHUNK_MASK;

        // Allocate new chunk if needed
        if (local_idx == 0 && chunks_[chunk_idx].load(std::memory_order_relaxed) == nullptr) {
            auto* new_chunk = new Chunk();
            chunks_[chunk_idx].store(new_chunk, std::memory_order_release);
        }

        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);

        // Find parent
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_idx = parent_it->second;
            height = get_height_internal(parent_idx) + 1;
            chain_work = get_chain_work_internal(parent_idx) + 1;
        }

        // Build skip pointer
        uint32_t skip_idx = build_skip(parent_idx, height);

        // Store in chunk (SoA layout)
        chunk->parent_indices[local_idx] = parent_idx;
        chunk->skip_indices[local_idx] = skip_idx;
        chunk->heights[local_idx] = height;
        chunk->chain_works[local_idx] = chain_work;
        chunk->statuses[local_idx] = 0;
        chunk->versions[local_idx] = hdr.version;
        chunk->merkle_roots[local_idx] = hdr.merkle_root;
        chunk->timestamps[local_idx] = hdr.timestamp;
        chunk->bits[local_idx] = hdr.bits;
        chunk->nonces[local_idx] = hdr.nonce;

        hash_to_idx_[hash] = sz;

        // Publish the new element (release so readers see all the writes above)
        size_.store(sz + 1, std::memory_order_release);

        return sz;
    }

    // Internal accessors (no bounds check, no atomics - caller ensures validity)
    int32_t get_height_internal(uint32_t idx) const {
        uint32_t chunk_idx = idx >> CHUNK_BITS;
        uint32_t local_idx = idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);
        return chunk->heights[local_idx];
    }

    uint64_t get_chain_work_internal(uint32_t idx) const {
        uint32_t chunk_idx = idx >> CHUNK_BITS;
        uint32_t local_idx = idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);
        return chunk->chain_works[local_idx];
    }

public:
    // =========================================================================
    // Lock-free accessors for readers
    // Safe to call concurrently with add() - no locks needed
    // =========================================================================

    uint32_t get_parent_idx(uint32_t idx) const {
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return null_index;

        uint32_t chunk_idx = idx >> CHUNK_BITS;
        uint32_t local_idx = idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->parent_indices[local_idx];
    }

    int32_t get_height(uint32_t idx) const {
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return 0;

        uint32_t chunk_idx = idx >> CHUNK_BITS;
        uint32_t local_idx = idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->heights[local_idx];
    }

    uint32_t get_timestamp(uint32_t idx) const {
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return 0;

        uint32_t chunk_idx = idx >> CHUNK_BITS;
        uint32_t local_idx = idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->timestamps[local_idx];
    }

    // Lookup by hash (still needs lock for hash map access)
    uint32_t find_idx(hash_digest const& hash) const {
        std::shared_lock lock(mutex_);
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? it->second : null_index;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start_idx, n, visitor)
    // Lock-free traversal - only touches chunk arrays
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        uint32_t idx = start_idx;
        int count = 0;
        while (idx != null_index && count < max_steps) {
            uint32_t chunk_idx = idx >> CHUNK_BITS;
            uint32_t local_idx = idx & CHUNK_MASK;
            Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);

            visitor(chunk->timestamps[local_idx]);
            idx = chunk->parent_indices[local_idx];
            ++count;
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear(start_idx, target_height)
    // O(n) linear traversal - lock-free
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        // Get initial height
        uint32_t chunk_idx = start_idx >> CHUNK_BITS;
        uint32_t local_idx = start_idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        int current_height = chunk->heights[local_idx];

        if (target_height > current_height) {
            return null_index;
        }

        // Traverse back
        int steps = current_height - target_height;
        uint32_t idx = start_idx;
        while (steps-- > 0 && idx != null_index) {
            chunk_idx = idx >> CHUNK_BITS;
            local_idx = idx & CHUNK_MASK;
            chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
            idx = chunk->parent_indices[local_idx];
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip(start_idx, target_height)
    // O(log n) with skip pointers - lock-free
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        uint32_t chunk_idx = start_idx >> CHUNK_BITS;
        uint32_t local_idx = start_idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);

        if (target_height > chunk->heights[local_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = chunk->heights[local_idx];

        while (height_walk > target_height) {
            chunk_idx = idx >> CHUNK_BITS;
            local_idx = idx & CHUNK_MASK;
            chunk = chunks_[chunk_idx].load(std::memory_order_acquire);

            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (chunk->skip_indices[local_idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = chunk->skip_indices[local_idx];
                height_walk = height_skip;
            } else {
                idx = chunk->parent_indices[local_idx];
                --height_walk;
            }
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor(idx_a, idx_b)
    // Lock-free
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        if (idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        // Get heights
        uint32_t chunk_idx_a = idx_a >> CHUNK_BITS;
        uint32_t local_idx_a = idx_a & CHUNK_MASK;
        Chunk* chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
        int height_a = chunk_a->heights[local_idx_a];

        uint32_t chunk_idx_b = idx_b >> CHUNK_BITS;
        uint32_t local_idx_b = idx_b & CHUNK_MASK;
        Chunk* chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
        int height_b = chunk_b->heights[local_idx_b];

        // Equalize heights
        while (height_a > height_b) {
            chunk_idx_a = idx_a >> CHUNK_BITS;
            local_idx_a = idx_a & CHUNK_MASK;
            chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
            idx_a = chunk_a->parent_indices[local_idx_a];
            if (idx_a == null_index) return null_index;
            --height_a;
        }
        while (height_b > height_a) {
            chunk_idx_b = idx_b >> CHUNK_BITS;
            local_idx_b = idx_b & CHUNK_MASK;
            chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
            idx_b = chunk_b->parent_indices[local_idx_b];
            if (idx_b == null_index) return null_index;
            --height_b;
        }

        // Walk back until they meet
        while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
            chunk_idx_a = idx_a >> CHUNK_BITS;
            local_idx_a = idx_a & CHUNK_MASK;
            chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
            idx_a = chunk_a->parent_indices[local_idx_a];

            chunk_idx_b = idx_b >> CHUNK_BITS;
            local_idx_b = idx_b & CHUNK_MASK;
            chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
            idx_b = chunk_b->parent_indices[local_idx_b];
        }

        return (idx_a == idx_b) ? idx_a : null_index;
    }

    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }

    void clear() {
        std::unique_lock lock(mutex_);
        for (auto& chunk : chunks_) {
            Chunk* ptr = chunk.load(std::memory_order_relaxed);
            delete ptr;
            chunk.store(nullptr, std::memory_order_relaxed);
        }
        hash_to_idx_.clear();
        size_.store(0, std::memory_order_relaxed);
    }

private:
    // Build skip pointer (called during add, under lock)
    uint32_t build_skip(uint32_t parent_idx, int32_t height) {
        if (parent_idx == null_index) {
            return null_index;
        }
        return get_ancestor_skip_internal(parent_idx, get_skip_height(height));
    }

    // Internal get_ancestor_skip (no size check, used during add)
    uint32_t get_ancestor_skip_internal(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        uint32_t chunk_idx = start_idx >> CHUNK_BITS;
        uint32_t local_idx = start_idx & CHUNK_MASK;
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);

        if (target_height > chunk->heights[local_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = chunk->heights[local_idx];

        while (height_walk > target_height) {
            chunk_idx = idx >> CHUNK_BITS;
            local_idx = idx & CHUNK_MASK;
            chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);

            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (chunk->skip_indices[local_idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = chunk->skip_indices[local_idx];
                height_walk = height_skip;
            } else {
                idx = chunk->parent_indices[local_idx];
                --height_walk;
            }
        }
        return idx;
    }
};

}  // namespace soa_chunks

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_12_SOA_CHUNKS_HPP
