// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_13_SOA_CHUNKS_LOCKFREE_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_13_SOA_CHUNKS_LOCKFREE_HPP

#include "common.hpp"

#include <array>
#include <atomic>
#include <memory>

#include <boost/unordered/concurrent_flat_map.hpp>

// =============================================================================
// Implementation: SoA Chunks Lock-Free
//
// Key difference from 12-soa_chunks.hpp:
//   - Uses boost::concurrent_flat_map instead of unordered_flat_map + mutex
//   - find_idx() is completely lock-free (no shared_lock!)
//   - All traversal operations are lock-free
//   - add() uses CAS spinlock instead of mutex (lighter weight)
//
// Thread safety:
//   - Writers: CAS spinlock for add() - competing blocks serialize, but no
//     heavy mutex overhead. After spinlock acquired, re-check if block was
//     inserted while waiting.
//   - Readers: completely lock-free!
//     - concurrent_flat_map handles hash lookup
//     - atomic size_ + memory ordering handles array access
//     - atomic chunk pointers handle chunk access
// =============================================================================

namespace soa_chunks_lockfree {

// Configuration - CHUNK_SIZE must be power of 2 for fast division
static constexpr size_t CHUNK_BITS = 20;                        // 2^20 = 1,048,576
static constexpr size_t CHUNK_SIZE = size_t{1} << CHUNK_BITS;   // ~1M elements per chunk
static constexpr size_t CHUNK_MASK = CHUNK_SIZE - 1;
static constexpr size_t MAX_CHUNKS = 10;                        // ~10M blocks max

static constexpr uint32_t null_index = std::numeric_limits<uint32_t>::max();

struct chunk_location {
    uint32_t chunk_idx;
    uint32_t local_idx;
};

[[nodiscard]]
static constexpr chunk_location get_location(uint32_t idx) noexcept {
    return {
        uint32_t(idx >> CHUNK_BITS), 
        uint32_t(idx & CHUNK_MASK)
    };
}

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
    // Directory: fixed array of chunk pointers
    std::array<std::atomic<Chunk*>, MAX_CHUNKS> chunks_{};

    // Current size
    std::atomic<uint32_t> size_{0};

    // CAS spinlock for writers (lighter than mutex)
    std::atomic<bool> writing_{false};

    // Lock-free hash map!
    boost::concurrent_flat_map<hash_digest, uint32_t, hash_digest_hasher> hash_to_idx_;

public:
    block_index_store() {
        for (auto& chunk : chunks_) {
            chunk.store(nullptr, std::memory_order_relaxed);
        }
    }

    ~block_index_store() {
        for (auto& chunk : chunks_) {
            delete chunk.load(std::memory_order_relaxed);
        }
    }

    block_index_store(block_index_store const&) = delete;
    block_index_store& operator=(block_index_store const&) = delete;
    block_index_store(block_index_store&&) = delete;
    block_index_store& operator=(block_index_store&&) = delete;

    // Add a new block (CAS spinlock for competing blocks)
    uint32_t add(hash_digest const& hash, header_data const& hdr) {
        // Fast path: check if already exists (lock-free!)
        uint32_t existing_idx = null_index;
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            existing_idx = pair.second;
        });
        if (existing_idx != null_index) {
            return existing_idx;
        }

        // Acquire spinlock
        bool expected = false;
        while ( ! writing_.compare_exchange_weak(expected, true,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
            expected = false;  // Reset for next CAS attempt
        }

        // Re-check after acquiring spinlock (another thread may have inserted it)
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            existing_idx = pair.second;
        });
        if (existing_idx != null_index) {
            writing_.store(false, std::memory_order_release);
            return existing_idx;
        }

        // We have exclusive write access
        uint32_t sz = size_.load(std::memory_order_relaxed);
        auto const [chunk_idx, local_idx] = get_location(sz);

        // Allocate new chunk if needed
        if (local_idx == 0 && chunks_[chunk_idx].load(std::memory_order_relaxed) == nullptr) {
            chunks_[chunk_idx].store(new Chunk(), std::memory_order_release);
        }

        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);

        // Find parent
        uint32_t parent_idx = null_index;
        int32_t height = 0;
        uint64_t chain_work = 0;

        hash_to_idx_.visit(hdr.prev_block_hash, [&](auto const& pair) {
            parent_idx = pair.second;
        });

        if (parent_idx != null_index) {
            height = get_height_internal(parent_idx) + 1;
            chain_work = get_chain_work_internal(parent_idx) + 1;
        }

        uint32_t skip_idx = build_skip(parent_idx, height);

        // Store in chunk
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

        // Insert into concurrent map
        hash_to_idx_.insert({hash, sz});

        // Publish size (release so readers see all writes above)
        size_.store(sz + 1, std::memory_order_release);

        // Release spinlock
        writing_.store(false, std::memory_order_release);

        return sz;
    }

private:

    int32_t get_height_internal(uint32_t idx) const {
        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);
        return chunk->heights[local_idx];
    }

    uint64_t get_chain_work_internal(uint32_t idx) const {
        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);
        return chunk->chain_works[local_idx];
    }

public:
    // =========================================================================
    // LOCK-FREE lookup by hash - NO MUTEX!
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
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return null_index;

        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->parent_indices[local_idx];
    }

    int32_t get_height(uint32_t idx) const {
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return 0;

        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->heights[local_idx];
    }

    uint32_t get_timestamp(uint32_t idx) const {
        uint32_t sz = size_.load(std::memory_order_acquire);
        if (idx >= sz) return 0;

        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->timestamps[local_idx];
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back - lock-free, chunk-optimized
    // - Read size_ once at start (snapshot for safety)
    // - Outer loop: one iteration per chunk we touch
    // - Inner loop: process blocks within same chunk (no atomic reload)
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        // Snapshot size once - if start_idx is valid, all ancestors are committed
        uint32_t const sz = size_.load(std::memory_order_acquire);
        if (start_idx >= sz || max_steps <= 0) {
            return;
        }

        uint32_t idx = start_idx;
        int remaining = max_steps;

        // Outer loop: one iteration per chunk
        while (idx != null_index && remaining > 0) {
            auto loc = get_location(idx);
            uint32_t const current_chunk = loc.chunk_idx;
            Chunk* chunk = chunks_[current_chunk].load(std::memory_order_acquire);

            // Inner loop: process blocks within this chunk
            do {
                visitor(chunk->timestamps[loc.local_idx]);
                idx = chunk->parent_indices[loc.local_idx];
                --remaining;

                if (idx == null_index || remaining <= 0) {
                    return;
                }

                // Check if next index is still in same chunk
                loc = get_location(idx);
                if (loc.chunk_idx != current_chunk) {
                    break;  // Exit to outer loop to load new chunk
                }
            } while (true);
        }
    }

    // =========================================================================
    // PRIMITIVE 2a: get_ancestor_linear - lock-free, chunk-optimized
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        uint32_t const sz = size_.load(std::memory_order_acquire);
        if (start_idx >= sz || start_idx == null_index || target_height < 0) {
            return null_index;
        }

        auto loc = get_location(start_idx);
        Chunk* chunk = chunks_[loc.chunk_idx].load(std::memory_order_acquire);
        int current_height = chunk->heights[loc.local_idx];

        if (target_height > current_height) {
            return null_index;
        }

        int remaining = current_height - target_height;
        uint32_t idx = start_idx;

        // Outer loop: one iteration per chunk
        while (remaining > 0 && idx != null_index) {
            loc = get_location(idx);
            uint32_t const current_chunk = loc.chunk_idx;
            chunk = chunks_[current_chunk].load(std::memory_order_acquire);

            // Inner loop: process within this chunk
            do {
                idx = chunk->parent_indices[loc.local_idx];
                --remaining;

                if (remaining <= 0 || idx == null_index) {
                    return idx;
                }

                loc = get_location(idx);
                if (loc.chunk_idx != current_chunk) {
                    break;
                }
            } while (true);
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 2b: get_ancestor_skip - lock-free, chunk-optimized
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        uint32_t const sz = size_.load(std::memory_order_acquire);
        if (start_idx >= sz || start_idx == null_index || target_height < 0) {
            return null_index;
        }

        auto loc = get_location(start_idx);
        uint32_t current_chunk = loc.chunk_idx;
        Chunk* chunk = chunks_[current_chunk].load(std::memory_order_acquire);

        if (target_height > chunk->heights[loc.local_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = chunk->heights[loc.local_idx];

        while (height_walk > target_height) {
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (chunk->skip_indices[loc.local_idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = chunk->skip_indices[loc.local_idx];
                height_walk = height_skip;
            } else {
                idx = chunk->parent_indices[loc.local_idx];
                --height_walk;
            }

            if (height_walk <= target_height) {
                break;
            }

            // Update location, reload chunk only if needed
            loc = get_location(idx);
            if (loc.chunk_idx != current_chunk) {
                current_chunk = loc.chunk_idx;
                chunk = chunks_[current_chunk].load(std::memory_order_acquire);
            }
        }
        return idx;
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor - lock-free, chunk-optimized
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        uint32_t const sz = size_.load(std::memory_order_acquire);
        if (idx_a >= sz || idx_b >= sz || idx_a == null_index || idx_b == null_index) {
            return null_index;
        }

        auto loc_a = get_location(idx_a);
        uint32_t chunk_idx_a = loc_a.chunk_idx;
        Chunk* chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
        int height_a = chunk_a->heights[loc_a.local_idx];

        auto loc_b = get_location(idx_b);
        uint32_t chunk_idx_b = loc_b.chunk_idx;
        Chunk* chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
        int height_b = chunk_b->heights[loc_b.local_idx];

        // Equalize height_a down to height_b
        while (height_a > height_b) {
            idx_a = chunk_a->parent_indices[loc_a.local_idx];
            if (idx_a == null_index) return null_index;
            --height_a;

            loc_a = get_location(idx_a);
            if (loc_a.chunk_idx != chunk_idx_a) {
                chunk_idx_a = loc_a.chunk_idx;
                chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
            }
        }

        // Equalize height_b down to height_a
        while (height_b > height_a) {
            idx_b = chunk_b->parent_indices[loc_b.local_idx];
            if (idx_b == null_index) return null_index;
            --height_b;

            loc_b = get_location(idx_b);
            if (loc_b.chunk_idx != chunk_idx_b) {
                chunk_idx_b = loc_b.chunk_idx;
                chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
            }
        }

        // Walk back in parallel until they meet
        while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
            idx_a = chunk_a->parent_indices[loc_a.local_idx];
            idx_b = chunk_b->parent_indices[loc_b.local_idx];

            if (idx_a == idx_b) {
                return idx_a;
            }

            if (idx_a != null_index) {
                loc_a = get_location(idx_a);
                if (loc_a.chunk_idx != chunk_idx_a) {
                    chunk_idx_a = loc_a.chunk_idx;
                    chunk_a = chunks_[chunk_idx_a].load(std::memory_order_acquire);
                }
            }

            if (idx_b != null_index) {
                loc_b = get_location(idx_b);
                if (loc_b.chunk_idx != chunk_idx_b) {
                    chunk_idx_b = loc_b.chunk_idx;
                    chunk_b = chunks_[chunk_idx_b].load(std::memory_order_acquire);
                }
            }
        }

        return (idx_a == idx_b) ? idx_a : null_index;
    }

    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }

    // void clear() {
    //     std::lock_guard lock(write_mutex_);
    //     for (auto& chunk : chunks_) {
    //         delete chunk.load(std::memory_order_relaxed);
    //         chunk.store(nullptr, std::memory_order_relaxed);
    //     }
    //     hash_to_idx_.clear();
    //     size_.store(0, std::memory_order_relaxed);
    // }

private:
    uint32_t build_skip(uint32_t parent_idx, int32_t height) {
        if (parent_idx == null_index) {
            return null_index;
        }
        return get_ancestor_skip_internal(parent_idx, get_skip_height(height));
    }

    // Internal version: called during add() under spinlock, uses relaxed ordering
    uint32_t get_ancestor_skip_internal(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
            return null_index;
        }

        auto loc = get_location(start_idx);
        uint32_t current_chunk = loc.chunk_idx;
        Chunk* chunk = chunks_[current_chunk].load(std::memory_order_relaxed);

        if (target_height > chunk->heights[loc.local_idx]) {
            return null_index;
        }

        uint32_t idx = start_idx;
        int height_walk = chunk->heights[loc.local_idx];

        while (height_walk > target_height) {
            int height_skip = get_skip_height(height_walk);
            int height_skip_prev = get_skip_height(height_walk - 1);

            if (chunk->skip_indices[loc.local_idx] != null_index &&
                (height_skip == target_height ||
                 (height_skip > target_height &&
                  !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
                idx = chunk->skip_indices[loc.local_idx];
                height_walk = height_skip;
            } else {
                idx = chunk->parent_indices[loc.local_idx];
                --height_walk;
            }

            if (height_walk <= target_height) {
                break;
            }

            // Reload chunk only if needed
            loc = get_location(idx);
            if (loc.chunk_idx != current_chunk) {
                current_chunk = loc.chunk_idx;
                chunk = chunks_[current_chunk].load(std::memory_order_relaxed);
            }
        }
        return idx;
    }
};

}  // namespace soa_chunks_lockfree

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_13_SOA_CHUNKS_LOCKFREE_HPP
