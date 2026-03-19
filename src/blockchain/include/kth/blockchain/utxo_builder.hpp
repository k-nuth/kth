// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
#define KTH_BLOCKCHAIN_UTXO_BUILDER_HPP

#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>

#include <utxoz/types.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/database/databases/internal_database.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/infrastructure/math/hash.hpp>

// Fast hasher for outpoints during IBD.
// Since txid is already SHA256 (uniformly distributed), we just grab the
// first 8 bytes as size_t and mix in the output index.
struct point_fast_hasher {
    size_t operator()(kth::domain::chain::point const& p) const noexcept {
        size_t seed;
        std::memcpy(&seed, p.hash().data(), sizeof(seed));
        seed ^= size_t(p.index()) * 0x9e3779b97f4a7c15ULL;
        return seed;
    }
};

namespace kth::blockchain {

// =============================================================================
// Minimal block representation for UTXO indexing
// =============================================================================
// No mutex, no parsed scripts, no domain objects.
// Outputs store raw bytes (value + script) as a span into the original buffer.
// Inputs store only the outpoint being spent.
// =============================================================================

struct utxo_compact_block {
    struct output_entry {
        utxoz::raw_outpoint key;        // txid(32) + index(4) — UTXO-Z native key
        std::span<uint8_t const> raw;   // raw output bytes (value + script), points into source buffer
        bool coinbase;
    };

    struct input_entry {
        utxoz::raw_outpoint prev_key;   // prev_txid(32) + prev_index(4) — UTXO-Z native key
    };

    std::vector<output_entry> outputs;
    std::vector<input_entry> inputs;    // excludes coinbase inputs
};

// Parse a raw block (starting at the 80-byte header) into a utxo_compact_block.
// The returned spans point into raw_block, which must outlive the result.
[[nodiscard]]
KB_API expect<utxo_compact_block> parse_utxo_block(byte_span raw_block);

// =============================================================================
// Pre-serialized UTXO value for zero-copy insertion into UTXO-Z.
// =============================================================================
// Format: height(4) + mtp(4) + coinbase(1) + raw_output_bytes
// Fixed prefix is 9 bytes. Raw output starts at offset 9 to end of buffer.
// No size field needed — output size = total_size - 9.
// =============================================================================

struct utxo_raw_value {
    std::vector<uint8_t> data;      // serialized in format above
    uint32_t height;                // also passed separately to UTXO-Z insert()
};

// =============================================================================
// UTXO Raw Delta (zero-copy path)
// =============================================================================
// Like utxo_delta, but uses UTXO-Z native keys (raw_outpoint) and
// pre-serialized byte vectors. No domain objects anywhere.
// =============================================================================

// Fast hasher for raw_outpoint (36 bytes).
// Since txid is already SHA256 (uniform), grab first 8 bytes + mix in index.
struct outpoint_fast_hasher {
    size_t operator()(utxoz::raw_outpoint const& k) const noexcept {
        size_t seed;
        std::memcpy(&seed, k.data(), sizeof(seed));
        uint32_t idx;
        std::memcpy(&idx, k.data() + 32, sizeof(idx));
        seed ^= size_t(idx) * 0x9e3779b97f4a7c15ULL;
        return seed;
    }
};

struct KB_API utxo_raw_delta {
    using key_t = utxoz::raw_outpoint;
    using hasher_t = outpoint_fast_hasher;

    boost::unordered_flat_map<key_t, utxo_raw_value, hasher_t> inserts;
    boost::unordered_flat_map<key_t, uint32_t, hasher_t> deletes;

    // Bloom filter skip counters (accumulated across merge)
    size_t bloom_skipped_inserts = 0;
    size_t bloom_skipped_deletes = 0;

    void merge(utxo_raw_delta&& other);
    void clear();

    [[nodiscard]]
    bool empty() const;

    [[nodiscard]]
    size_t insert_count() const { return inserts.size(); }

    [[nodiscard]]
    size_t delete_count() const { return deletes.size(); }
};

// Process a compact block into a raw delta (zero-copy path).
// Raw output bytes are serialized directly into UTXO-Z storage format.
// When bloom is provided, outputs/inputs not in the filter are skipped.
[[nodiscard]]
KB_API utxo_raw_delta process_compact_block_utxos(
    utxo_compact_block const& block,
    uint32_t height,
    uint32_t median_time_past,
    database::utxo_bloom_filter const* bloom = nullptr
);

// =============================================================================
// UTXO Delta (domain object path — used by sequential_direct strategy)
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
    using hasher_t = point_fast_hasher;

    boost::unordered_flat_map<point_t, entry_t, hasher_t> inserts;
    boost::unordered_flat_map<point_t, uint32_t, hasher_t> deletes;  // point -> height (for traceability)

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

// =============================================================================
// Bloom Filter Helpers (UTXO skip-insert optimization)
// =============================================================================

/// Build a bloom filter from the current UTXO set and save it to disk.
/// File: {data_dir}/utxo_bloom_{checkpoint_height}.dat
/// @return true on success
[[nodiscard]]
KB_API bool save_utxo_bloom(
    block_chain& chain,
    std::filesystem::path const& data_dir,
    uint32_t checkpoint_height
);

/// Load the embedded bloom filter from the executable's .rodata section.
/// The bloom data is compiled into the binary via .incbin (assembly).
/// @return shared_ptr to filter, or nullptr on parse error
[[nodiscard]]
KB_API std::shared_ptr<database::utxo_bloom_filter const> load_utxo_bloom();

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
