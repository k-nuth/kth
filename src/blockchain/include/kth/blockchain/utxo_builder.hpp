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

#include <spdlog/spdlog.h>

#include <utxoz/types.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/database/block_undo.hpp>
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

// Forward declaration (capture_block_undo reads UTXO-Z through the chain).
struct block_chain;

// =============================================================================
// Minimal block representation for UTXO indexing
// =============================================================================
// No mutex, no parsed scripts, no domain objects.
// Outputs store raw bytes (value + script) as a span into the original buffer.
// Inputs store only the outpoint being spent.
// =============================================================================

/// Compact UTXO reference: points to the transaction in blk*.dat flat files.
/// In compact mode, this 8-byte value replaces the full serialized output data.
struct compact_utxo_ref {
    uint32_t file_number;   // blk*.dat file number
    uint32_t tx_offset;     // absolute byte offset of the tx within that file
};
static_assert(sizeof(compact_utxo_ref) == 8);

struct utxo_compact_block {
    struct output_entry {
        utxoz::raw_outpoint key;        // txid(32) + index(4) — UTXO-Z native key
        std::span<uint8_t const> raw;   // raw output bytes (value + script), points into source buffer
        bool coinbase;
        uint32_t tx_start{0};           // offset of tx within the raw block (for compact mode)
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
// Format: raw_output_bytes (wire) + height(4) + mtp(4) + coinbase(1) — matches utxo_entry
// Raw output starts at offset 0; the 9-byte fixed metadata follows it.
// No size field needed — the wire output is self-delimiting.
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
// In compact mode, file_number and block_data_pos are used to build compact refs.
// NOTE: file_number is int16_t (matching header_index::get_file_number()) which limits
// to 32767 blk*.dat files (~4 TB at 134 MB/file). Sufficient for current chain sizes.
// If BCH grows past this, widen to int32_t in both header_index and here.
[[nodiscard]]
KB_API utxo_raw_delta process_compact_block_utxos(
    utxo_compact_block const& block,
    uint32_t height,
    uint32_t median_time_past,
    int16_t file_number,
    uint32_t block_data_pos,
    database::utxo_bloom_filter const* bloom = nullptr
);

// Build the undo record for one block: every output the block spends that
// existed BEFORE it, captured so the block can later be disconnected.
//
// `block_delta` is the block's OWN delta (from process_compact_block_utxos), not
// a merged batch delta: outputs created and spent inside the same block have
// already cancelled out there, and that is correct — they did not exist before
// the block, so disconnecting must not restore them.
//
// Values are resolved in two places because a spent output's parent may not be
// in UTXO-Z yet:
//   - `batch_delta` — the accumulated delta for blocks already processed in this
//     batch but not yet applied; a parent created there is still only in memory.
//   - UTXO-Z — everything older, read verbatim (no resolution) via find_raw,
//     honouring its two-phase contract (a miss is queued, not absent).
//
// Returns an error if any spent output cannot be located, since undo data that
// silently drops entries would corrupt the UTXO set on disconnect.
// `source` is anything exposing the two raw lookups this needs:
//     std::expected<utxoz_database::raw_stored, result_code> find_utxo_raw(key, height)
//     pair<map<key, raw_stored>, vector<key>>              utxo_process_pending_lookups_raw()
// block_chain satisfies it in production; a test can substitute a thin adapter
// over a utxoz_database, which is what keeps this function directly testable.
template <typename UtxoSource>
[[nodiscard]]
std::expected<database::block_undo, database::result_code> capture_block_undo(
    utxo_raw_delta const& block_delta,
    utxo_raw_delta const& batch_delta,
    UtxoSource& source,
    uint32_t height
) {
    database::block_undo undo;
    undo.spent.reserve(block_delta.deletes.size());

    // Spent outputs still missing after the first pass, to be resolved by the
    // deferred sweep (find_raw's key_not_found only means "queued").
    std::vector<utxoz::raw_outpoint> deferred;

    for (auto const& [key, _] : block_delta.deletes) {
        // Parent created earlier in this same batch: it lives only in the
        // in-flight delta, UTXO-Z has not seen it yet.
        if (auto it = batch_delta.inserts.find(key); it != batch_delta.inserts.end()) {
            undo.spent.push_back({key, it->second.data, it->second.height});
            continue;
        }

        auto stored = source.find_utxo_raw(key, height);
        if (stored) {
            undo.spent.push_back({key, std::move(stored->value), stored->height});
        } else {
            deferred.push_back(key);
        }
    }

    if ( ! deferred.empty()) {
        auto [resolved, missing] = source.utxo_process_pending_lookups_raw();

        for (auto const& key : deferred) {
            auto it = resolved.find(key);
            if (it == resolved.end()) {
                // The block spends an output the UTXO set does not have. Either
                // the block should not have validated, or the set is corrupt —
                // either way, undo data that silently dropped it would corrupt
                // the set further on disconnect.
                spdlog::error("[utxo_builder] capture_block_undo: spent output not found at height {} "
                    "({} of {} deferred lookups unresolved) — cannot build undo data",
                    height, missing.size(), deferred.size());
                return std::unexpected(database::result_code::key_not_found);
            }
            undo.spent.push_back({key, it->second.value, it->second.height});
        }
    }

    return undo;
}

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

/// Whether this build embeds a UTXO bloom filter (KTH_HAS_EMBEDDED_BLOOM).
/// Build-info: with an embedded bloom, IBD skips inserting outputs known to be
/// spent before the checkpoint; without it, IBD runs unoptimized. Single source
/// of truth for the node's --version / startup banner.
[[nodiscard]]
KB_API bool embedded_bloom_available();

/// The checkpoint height baked into the embedded bloom filter, if any.
/// @return the height, or 0 when this build has no embedded bloom.
[[nodiscard]]
KB_API uint32_t embedded_bloom_checkpoint_height();

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
