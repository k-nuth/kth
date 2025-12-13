// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_09_CFM_HASH_HPP
#define KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_09_CFM_HASH_HPP

#include "common.hpp"

#include <boost/unordered/concurrent_node_map.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>



// =============================================================================
// Implementation 4: Concurrent Flat Map with Hash-based parent (CFM-Hash)
// Uses hash_digest instead of pointers for parent reference
// - No pointer stability needed (flat map is faster)
// - Thread-safe via concurrent_flat_map
// - Traversal requires hash lookup per step (tradeoff for concurrency)
// =============================================================================

namespace cfm_hash {

// Null hash constant for "no parent"
inline constexpr hash_digest null_hash{};

struct block_index {
    // Parent hash instead of pointer - no pointer stability needed!
    hash_digest pprev_hash = null_hash;

    // Skip hash for O(log n) ancestor lookup (like BCHN's pskip)
    hash_digest pskip_hash = null_hash;

    // Height
    int32_t height = 0;

    // Header data
    header_data hdr{};

    // Chain work (simplified)
    uint64_t chain_work = 0;

    // Status flags
    uint32_t status = 0;

    block_index() = default;
    explicit block_index(header_data const& h) : hdr(h) {}
};

class block_index_store {
    boost::concurrent_flat_map<hash_digest, block_index, hash_digest_hasher> map_;

public:
    // Add a new block - returns true if inserted
    bool add(hash_digest const& hash, header_data const& hdr) {
        // First, get parent info if exists
        int32_t parent_height = -1;
        uint64_t parent_work = 0;

        map_.visit(hdr.prev_block_hash, [&](auto const& pair) {
            parent_height = pair.second.height;
            parent_work = pair.second.chain_work;
        });

        // Construct block_index with hash-based parent reference
        block_index new_idx(hdr);
        new_idx.pprev_hash = hdr.prev_block_hash;  // Store hash, not pointer!
        new_idx.height = parent_height + 1;
        new_idx.chain_work = parent_work + 1;

        // TODO: Calculate pskip_hash for O(log n) ancestor lookup
        // For now, leave it as null_hash (will use linear traversal)

        return map_.emplace(hash, std::move(new_idx));
    }

    // Check if block exists
    bool contains(hash_digest const& hash) const {
        bool found = false;
        map_.visit(hash, [&found](auto const&) {
            found = true;
        });
        return found;
    }

    // Get height (via visit)
    bool get_height(hash_digest const& hash, int32_t& out_height) const {
        bool found = false;
        map_.visit(hash, [&](auto const& pair) {
            out_height = pair.second.height;
            found = true;
        });
        return found;
    }

    // Get parent hash (via visit)
    bool get_parent_hash(hash_digest const& hash, hash_digest& out_parent) const {
        bool found = false;
        map_.visit(hash, [&](auto const& pair) {
            out_parent = pair.second.pprev_hash;
            found = true;
        });
        return found;
    }

    // Traverse to parent - returns true if parent exists
    // This is the key operation: lookup by hash instead of pointer chase
    bool traverse_to_parent(hash_digest const& current, hash_digest& out_parent) const {
        bool found = false;
        map_.visit(current, [&](auto const& pair) {
            out_parent = pair.second.pprev_hash;
            found = true;
        });
        // Check if parent is null (genesis block)
        if (found && out_parent == null_hash) {
            return false;  // No parent
        }
        return found;
    }

    // Walk back N blocks from given hash (returns count)
    int traverse_back(hash_digest const& start_hash, int max_steps) const {
        hash_digest current = start_hash;
        int count = 0;
        while (count < max_steps) {
            hash_digest parent;
            if (!traverse_to_parent(current, parent)) {
                break;  // Reached genesis or not found
            }
            current = parent;
            ++count;
        }
        return count;
    }

    // =========================================================================
    // PRIMITIVE 1: traverse_back(start_hash, n, visitor)
    // Walk back N blocks, executing visitor(block_index const&) on each
    // NOTE: Each step requires a hash lookup (O(1) but slower than pointer chase)
    // =========================================================================
    template<typename Visitor>
    void traverse_back_visit(hash_digest const& start_hash, int max_steps, Visitor&& visitor) const {
        hash_digest current = start_hash;
        int count = 0;
        while (count < max_steps) {
            bool found = false;
            hash_digest parent;
            map_.visit(current, [&](auto const& pair) {
                visitor(pair.second);
                parent = pair.second.pprev_hash;
                found = true;
            });
            if (!found || parent == null_hash) {
                break;
            }
            current = parent;
            ++count;
        }
    }

    // =========================================================================
    // PRIMITIVE 2: get_ancestor_linear(start_hash, target_height)
    // Jump to ancestor at specific height
    // Returns hash of ancestor (or null_hash if not found)
    // =========================================================================
    hash_digest get_ancestor_linear(hash_digest const& start_hash, int target_height) const {
        if (target_height < 0) {
            return null_hash;
        }

        // Get current height
        int32_t current_height = -1;
        map_.visit(start_hash, [&](auto const& pair) {
            current_height = pair.second.height;
        });

        if (current_height < 0 || target_height > current_height) {
            return null_hash;
        }

        // Walk back to target height
        hash_digest current = start_hash;
        while (current_height > target_height) {
            hash_digest parent = null_hash;
            map_.visit(current, [&](auto const& pair) {
                parent = pair.second.pprev_hash;
            });
            if (parent == null_hash) {
                return null_hash;
            }
            current = parent;
            --current_height;
        }
        return current;
    }

    // =========================================================================
    // PRIMITIVE 3: find_common_ancestor(hash_a, hash_b)
    // Find the last common ancestor of two blocks
    // Returns hash of common ancestor (or null_hash if not found)
    // =========================================================================
    hash_digest find_common_ancestor(hash_digest const& hash_a, hash_digest const& hash_b) const {
        // Get heights
        int32_t height_a = -1, height_b = -1;
        map_.visit(hash_a, [&](auto const& pair) { height_a = pair.second.height; });
        map_.visit(hash_b, [&](auto const& pair) { height_b = pair.second.height; });

        if (height_a < 0 || height_b < 0) {
            return null_hash;
        }

        hash_digest a = hash_a;
        hash_digest b = hash_b;

        // Equalize heights
        while (height_a > height_b) {
            hash_digest parent = null_hash;
            map_.visit(a, [&](auto const& pair) { parent = pair.second.pprev_hash; });
            if (parent == null_hash) return null_hash;
            a = parent;
            --height_a;
        }
        while (height_b > height_a) {
            hash_digest parent = null_hash;
            map_.visit(b, [&](auto const& pair) { parent = pair.second.pprev_hash; });
            if (parent == null_hash) return null_hash;
            b = parent;
            --height_b;
        }

        // Walk back in parallel until they meet
        while (a != b) {
            hash_digest parent_a = null_hash, parent_b = null_hash;
            map_.visit(a, [&](auto const& pair) { parent_a = pair.second.pprev_hash; });
            map_.visit(b, [&](auto const& pair) { parent_b = pair.second.pprev_hash; });
            if (parent_a == null_hash || parent_b == null_hash) {
                return null_hash;
            }
            a = parent_a;
            b = parent_b;
        }

        return a;
    }

    // Generic visit
    template<typename F>
    bool visit(hash_digest const& hash, F&& func) const {
        return map_.visit(hash, std::forward<F>(func));
    }

    template<typename F>
    bool visit(hash_digest const& hash, F&& func) {
        return map_.visit(hash, std::forward<F>(func));
    }

    size_t size() const {
        return map_.size();
    }

    void clear() {
        map_.clear();
    }
};

}  // namespace cfm_hash


#endif  // KTH_BLOCKCHAIN_TEST_BLOCK_INDEX_BENCH_09_CFM_HASH_HPP
