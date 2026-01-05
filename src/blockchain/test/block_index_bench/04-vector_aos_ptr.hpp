// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_04_VECTOR_AOS_PTR_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_04_VECTOR_AOS_PTR_HPP

#include "common.hpp"

#include <cassert>
#include <shared_mutex>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>


// =============================================================================
// Implementation 2d: Vector + AoS with pointers (like BCHN but in a vector)
// Test: is the slowdown from index lookup or from struct stride?
//
// Uses pointers like BCHN but stores blocks in a vector.
// IMPORTANT: Must use reserve() + emplace_back to ensure pointer stability
// (no reallocation after first insert)
//
// CURRENT BEST: ~31ns for 100-block traversal (M4 MacBook Pro)
//
// TODO: Optimization ideas to beat this implementation:
//
// 1. PREFETCHING MANUAL
//    El traversal es predecible (para cadena lineal, pprev apunta a blocks_[idx-1]).
//    Usar __builtin_prefetch(walk->pprev) para traer el próximo bloque a cache
//    mientras procesamos el actual.
//
// 2. LAYOUT DEL STRUCT OPTIMIZADO PARA CACHE
//    Actualmente pprev está al inicio, pero el struct es ~104 bytes. Opciones:
//    - Poner pprev en su propia cache line (alineación a 64 bytes)
//    - Hot/cold split: separar campos frecuentes de infrecuentes
//
// 3. SOA HÍBRIDO CON PUNTEROS (más prometedor)
//    Combinar lo mejor de SoA y punteros:
//      std::vector<block_index*> parent_ptrs_;  // 8-byte stride para traversal
//      std::vector<block_index> blocks_;        // Full data
//    En lugar de walk->pprev (104-byte stride), acceder a parent_ptrs_[idx] (8-byte stride).
//
// 4. LOOP UNROLLING MANUAL
//    Desenrollar el loop de traversal:
//      while (count + 4 <= max_steps) {
//          walk = walk->pprev->pprev->pprev->pprev;
//          count += 4;
//      }
//
// 5. ELIMINAR BRANCH DEL NULL CHECK
//    Para cadenas donde sabemos que no hay nulls intermedios (99.9% de los casos),
//    usar sentinel en lugar de nullptr.
//
// 6. __restrict__ HINTS
//    Decirle al compilador que los punteros no tienen aliasing.
//
// =============================================================================

namespace vector_aos_ptr {

struct block_index {
    // Pointer to parent (like BCHN)
    block_index* pprev = nullptr;

    // Header data
    header_data hdr{};

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
    bool reserved_ = false;

public:
    // MUST call before any add() to ensure pointer stability
    void preallocate(size_t n) {
        std::unique_lock lock(mutex_);
        blocks_.reserve(n);
        hash_to_idx_.reserve(n);
        reserved_ = true;
    }

    // Add a new block, returns pointer
    block_index* add(hash_digest const& hash, header_data const& hdr) {
        std::unique_lock lock(mutex_);

        // Check if already exists
        auto it = hash_to_idx_.find(hash);
        if (it != hash_to_idx_.end()) {
            return &blocks_[it->second];
        }

        // Safety check: must have reserved to guarantee pointer stability
        assert(reserved_ && "Must call preallocate() before add()");
        assert(blocks_.size() < blocks_.capacity() && "Vector would reallocate!");

        uint32_t const idx = uint32_t(blocks_.size());

        // Find parent
        block_index* parent_ptr = nullptr;
        int32_t height = 0;
        uint64_t chain_work = 0;

        auto parent_it = hash_to_idx_.find(hdr.prev_block_hash);
        if (parent_it != hash_to_idx_.end()) {
            parent_ptr = &blocks_[parent_it->second];
            height = parent_ptr->height + 1;
            chain_work = parent_ptr->chain_work + 1;
        }

        blocks_.emplace_back(hdr);
        blocks_.back().pprev = parent_ptr;
        blocks_.back().height = height;
        blocks_.back().chain_work = chain_work;

        hash_to_idx_[hash] = idx;
        return &blocks_.back();
    }

    // Find by hash
    block_index* find(hash_digest const& hash) {
        std::shared_lock lock(mutex_);
        auto it = hash_to_idx_.find(hash);
        return (it != hash_to_idx_.end()) ? &blocks_[it->second] : nullptr;
    }

    // Traverse using pointers (like BCHN) - no lock version for benchmarking
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
    // Jump to ancestor at specific height
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

}  // namespace vector_aos_ptr

#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_04_VECTOR_AOS_PTR_HPP
