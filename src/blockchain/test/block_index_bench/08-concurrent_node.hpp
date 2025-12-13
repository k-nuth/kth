// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_08_CONCURRENT_NODE_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_08_CONCURRENT_NODE_HPP

#include "common.hpp"

#include <boost/unordered/concurrent_node_map.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>



// =============================================================================
// Implementation 3: Concurrent node map (boost::concurrent_node_map)
// Pointer stability + concurrent access without external mutex
//
// API follows visit() pattern:
// - All reads/writes MUST go through visit() callbacks
// - Pointer stability allows pprev to work, but field access needs visit()
// - visit() holds internal lock on the element during callback execution
// =============================================================================

namespace concurrent_node {

struct block_index {
    // Pointer to parent - relies on pointer stability of concurrent_node_map
    block_index* pprev = nullptr;

    // Skip list pointer
    block_index* pskip = nullptr;

    // Height
    int32_t height = 0;

    // Header data
    header_data hdr{};

    // Chain work (simplified)
    uint64_t chain_work = 0;

    // Status flags (can be modified after insertion via visit())
    uint32_t status = 0;

    block_index() = default;
    explicit block_index(header_data const& h) : hdr(h) {}
};

class block_index_store {
    boost::concurrent_node_map<hash_digest, block_index, hash_digest_hasher> map_;

public:
    // ==========================================================================
    // CORRECT API: All access through visit() callbacks
    // ==========================================================================

    // Add a new block - returns true if inserted, false if already exists
    bool add_to_block_index(hash_digest const& hash, header_data const& hdr) {
        // First, find parent info (if exists)
        block_index* parent_ptr = nullptr;
        int32_t parent_height = -1;
        uint64_t parent_work = 0;

        map_.visit(hdr.prev_block_hash, [&](auto const& pair) {
            parent_ptr = const_cast<block_index*>(&pair.second);
            parent_height = pair.second.height;
            parent_work = pair.second.chain_work;
        });

        // Construct block_index with all fields set BEFORE insertion
        block_index new_idx(hdr);
        new_idx.pprev = parent_ptr;
        new_idx.height = parent_height + 1;
        new_idx.chain_work = parent_work + 1;

        return map_.emplace(hash, std::move(new_idx));
    }

    // Read status safely through visit()
    bool get_status(hash_digest const& hash, uint32_t& out_status) const {
        bool found = false;
        map_.visit(hash, [&](auto const& pair) {
            out_status = pair.second.status;
            found = true;
        });
        return found;
    }

    // Write status safely through visit()
    bool set_status(hash_digest const& hash, uint32_t new_status) {
        bool found = false;
        map_.visit(hash, [&](auto& pair) {
            pair.second.status = new_status;
            found = true;
        });
        return found;
    }

    // Read height safely
    bool get_height(hash_digest const& hash, int32_t& out_height) const {
        bool found = false;
        map_.visit(hash, [&](auto const& pair) {
            out_height = pair.second.height;
            found = true;
        });
        return found;
    }

    // Generic visit for complex operations
    template<typename F>
    bool visit(hash_digest const& hash, F&& func) {
        return map_.visit(hash, std::forward<F>(func));
    }

    template<typename F>
    bool visit(hash_digest const& hash, F&& func) const {
        return map_.visit(hash, std::forward<F>(func));
    }

    // ==========================================================================
    // UNSAFE API: Returns raw pointers (for benchmarking/comparison only)
    // WARNING: Using returned pointer outside visit() is a data race!
    // ==========================================================================

    std::pair<block_index*, bool> add(hash_digest const& hash, header_data const& hdr) {
        block_index* parent_ptr = nullptr;
        map_.visit(hdr.prev_block_hash, [&parent_ptr](auto const& pair) {
            parent_ptr = const_cast<block_index*>(&pair.second);
        });

        block_index new_idx(hdr);
        new_idx.pprev = parent_ptr;
        new_idx.height = parent_ptr ? parent_ptr->height + 1 : 0;
        new_idx.chain_work = parent_ptr ? parent_ptr->chain_work + 1 : 0;

        bool const inserted = map_.emplace(hash, std::move(new_idx));

        block_index* idx_ptr = nullptr;
        map_.visit(hash, [&idx_ptr](auto const& pair) {
            idx_ptr = const_cast<block_index*>(&pair.second);
        });

        return {idx_ptr, inserted};
    }

    // UNSAFE: Returns raw pointer
    block_index* find(hash_digest const& hash) {
        block_index* result = nullptr;
        map_.visit(hash, [&result](auto const& pair) {
            result = const_cast<block_index*>(&pair.second);
        });
        return result;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start, n, visitor)
    // Walk back N blocks, executing visitor(block_index const&) on each
    // Uses pprev pointers (pointer stability guaranteed by concurrent_node_map)
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
        return map_.size();
    }

    void clear() {
        map_.clear();
    }
};

}  // namespace concurrent_node


#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_08_CONCURRENT_NODE_HPP
