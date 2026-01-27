// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
#define KTH_BLOCKCHAIN_UTXO_BUILDER_HPP

#include <cstdint>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/database/databases/internal_database.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/point.hpp>

namespace kth::blockchain {

// =============================================================================
// UTXO Delta
// =============================================================================
// Represents the UTXO changes from processing one or more blocks.
// - inserts: new UTXOs to add to the UTXO set
// - deletes: existing UTXOs to remove from the UTXO set
//
// Internal spends (where a tx spends an output created in the same block/batch)
// are resolved during processing and don't appear in either set.
// =============================================================================

struct KB_API utxo_delta {
    using point_t = domain::chain::point;
    using entry_t = database::utxo_entry;

    boost::unordered_flat_map<point_t, entry_t> inserts;
    boost::unordered_flat_map<point_t, uint32_t> deletes;  // point -> height (for traceability)

    // Merge another delta into this one (must be from a later block)
    void merge(utxo_delta&& other);

    // Clear both sets
    void clear();

    // Check if empty
    [[nodiscard]]
    bool empty() const;

    // Stats
    [[nodiscard]]
    size_t insert_count() const { return inserts.size(); }

    [[nodiscard]]
    size_t delete_count() const { return deletes.size(); }
};

// =============================================================================
// Block Processing (single block, can run in parallel)
// =============================================================================

[[nodiscard]]
KB_API utxo_delta process_block_utxos(
    domain::chain::block const& block,
    uint32_t height,
    uint32_t median_time_past
);

// =============================================================================
// Batch Processing with thread pool
// =============================================================================

struct block_with_context {
    domain::chain::block const* block;
    uint32_t height;
    uint32_t median_time_past;
};

// Process multiple blocks using the provided thread pool.
// Returns a coroutine that yields the merged delta.
[[nodiscard]]
KB_API ::asio::awaitable<utxo_delta> process_blocks_parallel(
    ::asio::thread_pool& pool,
    std::vector<block_with_context> const& blocks
);

// =============================================================================
// Apply Delta to Database
// =============================================================================
// Applies the merged delta to the database:
// 1. Remove all UTXOs in deletes
// 2. Insert all UTXOs in inserts
// Returns error code if any operation fails.
// =============================================================================

[[nodiscard]]
KB_API database::result_code apply_utxo_delta(
    database::internal_database& db,
    utxo_delta const& delta
);

// =============================================================================
// UTXO Set Builder (main entry point for building UTXO from stored blocks)
// =============================================================================
// Builds the UTXO set by processing blocks from start_height to end_height.
// - Reads blocks from the database
// - Processes them in batches (strategy determines parallelism)
// - Calculates median_time_past for each block
// - Applies the resulting delta to the database
// =============================================================================

// Forward declaration to avoid circular include
struct block_chain;

// Processing strategy for UTXO set building
enum class utxo_build_strategy {
    // Process 1000 blocks in parallel, merge internally, then apply to UTXO-Z
    parallel_batch,

    // Process 1000 blocks sequentially, merge internally, then apply to UTXO-Z
    sequential_batch,

    // Process 1 block at a time, apply directly to UTXO-Z
    // (pending deletions and compact every 1000 blocks)
    sequential_direct
};

[[nodiscard]]
KB_API ::asio::awaitable<database::result_code> build_utxo_set(
    block_chain& chain,
    ::asio::thread_pool& pool,
    uint32_t start_height,
    uint32_t end_height,
    utxo_build_strategy strategy = utxo_build_strategy::parallel_batch
);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
