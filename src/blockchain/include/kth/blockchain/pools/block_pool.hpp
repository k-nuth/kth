// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_POOL_HPP
#define KTH_BLOCKCHAIN_BLOCK_POOL_HPP

#include <cstddef>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/block_entry.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

/// This class is thread safe against concurrent filtering only.
/// There is no search within blocks of the block pool (just hashes).
/// The branch object contains chain query for new (leaf) block validation.
/// All pool blocks are valid, lacking only sufficient work for reorganzation.
class KB_API block_pool {
public:
    block_pool(size_t maximum_depth);

    // The number of blocks in the pool.
    size_t size() const;

    /// Add newly-validated block (work insufficient to reorganize).
    void add(block_const_ptr valid_block);

    /// Add root path of reorganized blocks (no branches).
    void add(block_const_ptr_list_const_ptr valid_blocks);

    /// Remove path of accepted blocks (sub-branches moved to root).
    void remove(block_const_ptr_list_const_ptr accepted_blocks);

    /// Purge branches rooted below top minus maximum depth.
    void prune(size_t top_height);

    /// Remove all message vectors that match block hashes.
    void filter(get_data_ptr message) const;

    /// Get the root path to and including the new block.
    /// This will be empty if the block already exists in the pool.
    branch::ptr get_path(block_const_ptr candidate_block) const;

protected:
    // A bidirectional map is used for efficient block and position retrieval.
    // This produces the effect of a circular buffer hash table of blocks.
    using block_entries = boost::bimaps::bimap<
        boost::bimaps::unordered_set_of<block_entry>,
        boost::bimaps::multiset_of<size_t>>;

    void prune(hash_list const& hashes, size_t minimum_height);
    bool exists(block_const_ptr candidate_block) const;
    block_const_ptr parent(block_const_ptr block) const;
    ////void log_content() const;

    // This is thread safe.
    size_t const maximum_depth_;

    // This is guarded against filtering concurrent to writing.
    block_entries blocks_;
#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif
};

} // namespace kth::blockchain

#endif
