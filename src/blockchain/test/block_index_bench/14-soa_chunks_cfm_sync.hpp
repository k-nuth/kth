// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_14_SOA_CHUNKS_CFM_SYNC_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_14_SOA_CHUNKS_CFM_SYNC_HPP

#include "common.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <memory>

#include <boost/unordered/concurrent_flat_map.hpp>

// =============================================================================
// Implementation: SoA Chunks with CFM Synchronization
//
// Key difference from 13-soa_chunks_lockfree.hpp:
//   - NO spinlock! CFM's bucket-level locking synchronizes everything
//   - Uses insert_and_visit to atomically check+insert+initialize
//   - Uses CFM's size() instead of our own atomic size_
//
// Why this works (discussion with Joaquín from Boost.Unordered):
//   1. insert_and_visit runs the visitor under the bucket lock
//   2. Our chunks don't reallocate (pre-allocated), so concurrent writes
//      to different indices are safe
//   3. Synchronization is transitive: if Thread A syncs with B, and B with C,
//      then A's writes are visible to C
//   4. Every block lookup acquires a bucket lock, providing synchronization
//      with whoever wrote that block
//
// Thread safety:
//   - Writers: CFM's bucket lock during insert_and_visit
//   - Readers: CFM's bucket lock during visit/cvisit provides acquire semantics
//   - Traversal: safe because we always look up a block before traversing,
//     and all ancestors were inserted (and synchronized) before the block
// =============================================================================

namespace soa_chunks_cfm_sync {

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

    // Index counter - only used for getting next index, not for bounds checking
    std::atomic<uint32_t> next_idx_{0};

    // Lock-free hash map - provides all synchronization!
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

    void preallocate(size_t n) {
        // Pre-allocate chunks to avoid allocation during insert
        size_t num_chunks = (n + CHUNK_SIZE - 1) / CHUNK_SIZE;
        for (size_t i = 0; i < num_chunks && i < MAX_CHUNKS; ++i) {
            if (chunks_[i].load(std::memory_order_relaxed) == nullptr) {
                chunks_[i].store(new Chunk(), std::memory_order_relaxed);
            }
        }
        hash_to_idx_.reserve(n);
    }

    // =========================================================================
    // ADD - Uses CFM's insert_and_visit for synchronization
    // No spinlock needed!
    // =========================================================================
    std::pair<bool, uint32_t> add(hash_digest const& hash, header_data const& hdr) {
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
            auto const [p_chunk_idx, p_local_idx] = get_location(parent_idx);
            Chunk* parent_chunk = chunks_[p_chunk_idx].load(std::memory_order_acquire);
            height = parent_chunk->heights[p_local_idx] + 1;
            chain_work = parent_chunk->chain_works[p_local_idx] + 1;
        } else {
            // std::cout << "Adding genesis block\n";
            // static constexpr char hex_chars[] = "0123456789abcdef";
            // constexpr size_t hash_size = hash_digest{}.size();
            // char hex_buffer[hash_size * 2];

            // auto const* const data = hdr.prev_block_hash.data();
            // for (size_t i = 0; i < hash_size; ++i) {
            //     uint8_t const byte = data[i];
            //     hex_buffer[i * 2] = hex_chars[byte >> 4];
            //     hex_buffer[i * 2 + 1] = hex_chars[byte & 0x0F];
            // }

            // std::cout << "prev_block_hash: ";
            // std::cout.write(hex_buffer, sizeof(hex_buffer));
            // std::cout.put('\n');
        }

        // Now do the insert with parent info already available
        hash_to_idx_.insert_and_visit(
            {hash, null_index},
            [&](auto& pair) {
                // Newly inserted - initialize everything
                uint32_t const idx = next_idx_.fetch_add(1, std::memory_order_relaxed);
                auto const [chunk_idx, local_idx] = get_location(idx);

                // Allocate chunk if needed (should be pre-allocated, but just in case)
                if (chunks_[chunk_idx].load(std::memory_order_relaxed) == nullptr) {
                    chunks_[chunk_idx].store(new Chunk(), std::memory_order_release);
                }

                Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_relaxed);

                uint32_t const skip_idx = build_skip(parent_idx, height);

                // Store in chunk (all writes happen under bucket lock)
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

        return {is_inserted, result_idx};
    }

    // =========================================================================
    // FIND - Lock-free lookup via CFM
    // The visit provides acquire semantics for all chunk data
    // =========================================================================
    uint32_t find_idx(hash_digest const& hash) const {
        uint32_t result = null_index;
        hash_to_idx_.visit(hash, [&](auto const& pair) {
            result = pair.second;
        });
        return result;
    }

    // =========================================================================
    // Accessors - No bounds checking needed!
    // If you have a valid index (from find_idx or traversal), the data is there.
    // =========================================================================
    // uint32_t get_parent_idx(uint32_t idx) const {
    //     auto [chunk_idx, local_idx] = get_location(idx);
    //     Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
    //     return chunk->parent_indices[local_idx];
    // }

    int32_t get_height(uint32_t idx) const {
        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->heights[local_idx];
    }

    uint32_t get_timestamp(uint32_t idx) const {
        auto [chunk_idx, local_idx] = get_location(idx);
        Chunk* chunk = chunks_[chunk_idx].load(std::memory_order_acquire);
        return chunk->timestamps[local_idx];
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back - chunk-optimized
    // No size check needed - if start_idx is valid, all ancestors are valid
    // =========================================================================
    template<typename Visitor>
    void traverse_back(uint32_t start_idx, int max_steps, Visitor&& visitor) const {
        if (start_idx == null_index || max_steps <= 0) {
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
    // PRIMITIVE 2a: get_ancestor_linear - chunk-optimized
    // =========================================================================
    uint32_t get_ancestor_linear(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
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
    // PRIMITIVE 2b: get_ancestor_skip - chunk-optimized
    // =========================================================================
    uint32_t get_ancestor_skip(uint32_t start_idx, int target_height) const {
        if (start_idx == null_index || target_height < 0) {
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
    // PRIMITIVE 3: find_common_ancestor - chunk-optimized
    // =========================================================================
    uint32_t find_common_ancestor(uint32_t idx_a, uint32_t idx_b) const {
        if (idx_a == null_index || idx_b == null_index) {
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
        return hash_to_idx_.size();  // Use CFM's size!
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

}  // namespace soa_chunks_cfm_sync

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_14_SOA_CHUNKS_CFM_SYNC_HPP
