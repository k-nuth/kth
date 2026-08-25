// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_UTXO_BUILDER_HPP
#define KTH_BLOCKCHAIN_UTXO_BUILDER_HPP

#include <cstdint>
#include <optional>
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

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/config/network.hpp>
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

/// UTXO storage reference: points to the transaction in blk*.dat flat files.
/// In reference mode, this 8-byte value replaces the full serialized output data.
struct reference_utxo_ref {
    uint32_t file_number;   // blk*.dat file number
    uint32_t tx_offset;     // absolute byte offset of the tx within that file
};
static_assert(sizeof(reference_utxo_ref) == 8);

struct utxo_compact_block {
    struct output_entry {
        utxoz::raw_outpoint key;        // txid(32) + index(4) — UTXO-Z native key
        std::span<uint8_t const> raw;   // raw output bytes (value + script), points into source buffer
        bool coinbase;
        uint32_t tx_start{0};           // offset of tx within the raw block (for reference mode)
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

// What merging one block's delta into a batch delta concluded. A second insert
// of a key the batch already carries is not a detail the merge can settle on its
// own: for the two blocks BIP30 grandfathers it is an authorized replacement, and
// anywhere else it is a consensus violation. Silently keeping one of the two --
// which is what an `emplace` does -- answers neither question and makes the batch
// partition decide the result (#695).
enum class delta_merge_result {
    // Nothing collided, or the collision was an authorized BIP30 replacement.
    ok,

    // A second insert arrived for a key already in the batch, with no consensus
    // authorization to replace it.
    unauthorized_duplicate,
};

struct KB_API utxo_raw_delta {
    using key_t = utxoz::raw_outpoint;
    using hasher_t = outpoint_fast_hasher;

    boost::unordered_flat_map<key_t, utxo_raw_value, hasher_t> inserts;
    boost::unordered_flat_map<key_t, uint32_t, hasher_t> deletes;

    // The keys this block is permitted to re-create over a live entry. Filled
    // when the block's delta is built, from the consensus rule itself
    // (is_bip30_exception over the block's own {hash, height}), and carried with
    // the operation from there on. A merge NEVER decides that a collision must
    // have been BIP30: the two grandfathered blocks are the only source of an
    // entry here, so a collision without one is a consensus violation and is
    // reported as such. Empty for every block but those two.
    boost::unordered_flat_set<key_t, hasher_t> authorized_replacements;

    // Bloom filter skip counters (accumulated across merge)
    size_t bloom_skipped_inserts = 0;
    size_t bloom_skipped_deletes = 0;

    // Merging one block into this batch. Either the whole of `other` is folded
    // in, or nothing is: an unauthorized duplicate is reported with this batch
    // left exactly as it was. Two passes rather than one, so the answer cannot
    // depend on the order `other.inserts` happens to iterate in -- a batch whose
    // outcome varies with a hash table's layout is the same class of defect as
    // one whose outcome varies with the partition (#695).
    delta_merge_result merge(utxo_raw_delta&& other);
    // Empties the batch, authorizations included. A licence that outlived the
    // delta it was granted for would authorize a replacement in whatever batch
    // reused the object.
    void clear();

    [[nodiscard]]
    bool empty() const;

    [[nodiscard]]
    size_t insert_count() const { return inserts.size(); }

    [[nodiscard]]
    size_t delete_count() const { return deletes.size(); }
};

// =============================================================================
// Applying a built delta
// =============================================================================
//
// What the store is asked to do with a delta that is already built and already
// carries whatever authorization it is entitled to. Everything that DECIDES stays
// with the caller: the consensus question (is_bip30_exception, over the block's
// own network, hash and height) is answered when the block's delta is built, and
// the transition record, the write window, mark_mutating/complete, the poisoned
// gate, on_fatal and the fatal logging stay in the build task. This does the two
// mutations, in order, and reports what happened.

enum class delta_apply_status {
    /// Everything asked for was done.
    success,

    /// The store faulted while withdrawing a displaced entry. `store_error`
    /// carries which fault -- recovery_required, which says the store has
    /// latched, is not the same instruction as a read that failed, so it is
    /// never flattened into a generic failure.
    withdrawal_failed,

    /// The withdrawal walk did not finish for some keys. A deletion batch would
    /// resend those; this cannot, because the insert that follows would land on a
    /// key still holding its old entry.
    withdrawal_unresolved,

    /// The inserts failed. `insert_error` carries the code for the same reason.
    insert_failed,
};

struct delta_apply_result {
    delta_apply_status status{delta_apply_status::success};

    /// Set when status is withdrawal_failed.
    std::optional<utxoz::error_code> store_error;

    /// Set when status is insert_failed.
    std::optional<database::result_code> insert_error;

    /// What the withdrawal actually did, for diagnosis and for a recovery that
    /// has to reconcile the store against the undo records. `absent` is not a
    /// fault: an authorized insert with nothing to overwrite is ordinary work.
    size_t erased{0};
    size_t absent{0};
    size_t unresolved{0};

    [[nodiscard]]
    bool ok() const { return status == delta_apply_status::success; }
};

// `chain` is anything exposing utxo_apply_deletes and apply_utxo_inserts_raw over
// a window the caller already opened and already marked mutating. Templated so
// the blockchain layer does not depend on the node layer to be exercised.
template <typename Chain, typename Window>
delta_apply_result apply_utxo_delta(
    Chain& chain,
    Window const& window,
    utxo_raw_delta const& delta,
    uint32_t delete_height
) {
    delta_apply_result result;

    // A BIP30 replacement cannot go in as an ordinary insert: the store holds the
    // entry it displaces and refuses to write over a live key. Withdraw first, so
    // the insert lands on an absent key exactly like every other one. Only the
    // licensed keys this delta actually re-creates.
    if ( ! delta.authorized_replacements.empty()) {
        std::vector<utxoz::deferred_deletion_entry> displaced;
        displaced.reserve(delta.authorized_replacements.size());
        for (auto const& key : delta.authorized_replacements) {
            if (delta.inserts.contains(key)) {
                displaced.emplace_back(key, delete_height);
            }
        }

        if ( ! displaced.empty()) {
            auto const progress = chain.utxo_apply_deletes(window, displaced);
            result.erased = progress.erased.size();
            result.absent = progress.absent.size();
            result.unresolved = progress.unresolved.size();

            // Every part of the answer is read, and they do not mean the same
            // thing: a fault and an unfinished walk stop the insert, an absent
            // key does not.
            if (progress.error) {
                result.status = delta_apply_status::withdrawal_failed;
                result.store_error = *progress.error;
                return result;
            }
            if ( ! progress.unresolved.empty()) {
                result.status = delta_apply_status::withdrawal_unresolved;
                return result;
            }
        }
    }

    if ( ! delta.empty()) {
        auto const code = chain.apply_utxo_inserts_raw(window, delta.inserts);
        if (code != database::result_code::success) {
            result.status = delta_apply_status::insert_failed;
            result.insert_error = code;
            return result;
        }
    }

    return result;
}

// Process a compact block into a raw delta (zero-copy path).
// Raw output bytes are serialized directly into UTXO-Z storage format.
// When bloom is provided, outputs/inputs not in the filter are skipped.
// In reference mode, file_number and block_data_pos are used to build reference refs.
// NOTE: file_number is int16_t (matching header_index::get_file_number()) which limits
// to 32767 blk*.dat files (~4 TB at 134 MB/file). Sufficient for current chain sizes.
// If BCH grows past this, widen to int32_t in both header_index and here.
[[nodiscard]]
/// @return the block's delta, or an error if the block cannot be referenced.
///         A negative `file_number` means the header index has no data for a
///         block whose UTXOs are being built — the index and the block store
///         disagree, which a crash can produce. It is NOT an impossible
///         precondition, so it does not get an assertion that evaporates in
///         Release: there, -1 would become UINT32_MAX and every reference in
///         this block would point at a file that will never exist.
[[nodiscard]]
KB_API expect<utxo_raw_delta> process_compact_block_utxos(
    utxo_compact_block const& block,
    hash_digest const& block_hash,
    uint32_t height,
    uint32_t median_time_past,
    domain::config::network network,
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
//     honouring its two-phase contract (a miss is unresolved, not absent).
//
// Fails rather than drop an entry, since undo data missing one would corrupt the
// UTXO set on disconnect. The two ways to fail are kept apart, because they mean
// different things:
//   key_not_found — the delta says an output is spent that the set does not
//                   have, once the batch was resolved against the older versions;
//   anything else — the source could not be read at all.
// Neither is interpreted here: this function does not know what the caller knows
// about the block. One that knows it passed validation can conclude more from
// the first than one that does not (see utxo_build_task).
//
// `source` is anything exposing the two raw lookups this needs:
//     std::expected<utxoz_database::raw_stored, result_code>  find_utxo_raw(key, height)
//     std::expected<utxoz_database::raw_resolution, result_code>
//                                        utxo_resolve_raw(span<lookup_request const>)
// block_chain satisfies it in production; a test can substitute a thin adapter
// over a utxoz_database — or one that fails on purpose, which is how the two
// branches above are covered.
template <typename UtxoSource>
[[nodiscard]]
std::expected<database::block_undo, database::result_code> capture_block_undo(
    utxo_raw_delta const& block_delta,
    utxo_raw_delta const& batch_delta,
    UtxoSource& source,
    uint32_t height
) {
    database::block_undo undo;
    undo.spent.reserve(block_delta.deletes.size() + block_delta.authorized_replacements.size());

    // Spent outputs still missing after the first pass, to be resolved by the
    // batch resolution (find_raw's not_resolved only means "the active versions
    // cannot answer").
    std::vector<utxoz::lookup_request> deferred;

    // A BIP30 replacement displaces a live entry, and nothing else records it:
    // the original output is never spent, so it is in no `deletes` list. Captured
    // FIRST, before the merge folds this block in and before anything is applied,
    // because after either the previous value is gone.
    //
    // The batch is asked before the store. When the original and the duplicate
    // share a batch, the entry the duplicate displaces was created by an earlier
    // block of that same batch and has not been published yet, so the store would
    // truthfully answer that it does not have it.
    //
    // Absence is not corruption. An exception block whose coinbase output the set
    // does not already hold is an authorized insert with nothing to overwrite --
    // which is what the second of the two looks like after a rewind past the
    // first. Nothing is recorded, and the disconnect will simply remove what the
    // block created.
    std::vector<utxoz::lookup_request> deferred_replacements;

    for (auto const& key : block_delta.authorized_replacements) {
        if (auto it = batch_delta.inserts.find(key); it != batch_delta.inserts.end()) {
            undo.spent.push_back({key, it->second.data, it->second.height});
            continue;
        }
        auto stored = source.find_utxo_raw(key, height);
        if (stored) {
            undo.spent.push_back({key, std::move(stored->value), stored->height});
            continue;
        }

        // not_resolved is not absence: it says the ACTIVE versions cannot answer,
        // and the entry a replacement displaces may well be in an older
        // generation. Accepting it as "nothing to keep" would lose the only copy
        // of that value, and the disconnect would then delete the key instead of
        // restoring it. Resolved below, exactly like a spent output.
        if (stored.error() == database::result_code::not_resolved) {
            deferred_replacements.emplace_back(key, height);
            continue;
        }

        if (stored.error() != database::result_code::key_not_found) {
            spdlog::error("[utxo_builder] capture_block_undo: reading the entry a BIP30 "
                "replacement displaces failed at height {}", height);
            return std::unexpected(stored.error());
        }
        // key_not_found is a proven absence, and for a replacement that is
        // ordinary: an authorized insert with nothing to overwrite.
    }

    if ( ! deferred_replacements.empty()) {
        auto resolved = source.utxo_resolve_raw(deferred_replacements);
        if ( ! resolved) {
            spdlog::error("[utxo_builder] capture_block_undo: resolving {} entry(ies) a BIP30 "
                "replacement displaces failed at height {}; no undo record is captured",
                deferred_replacements.size(), height);
            return std::unexpected(resolved.error());
        }

        // Absence AFTER resolution is the answer, not a fault -- unlike a spent
        // output, whose absence means the set and the delta disagree. Here it
        // means the set simply does not hold what this block re-creates.
        for (auto const& request : deferred_replacements) {
            if (auto it = resolved->found.find(request.key); it != resolved->found.end()) {
                undo.spent.push_back(database::spent_output{
                    request.key, it->second.value, it->second.height});
            }
        }
    }

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
            continue;
        }

        // Only not_resolved means "the active versions cannot say". Anything
        // else is a read that failed, and carrying it into the batch would turn
        // a storage error into a missing output — the caller would be told the
        // set does not have what the block spends, when what happened is that it
        // could not be asked.
        if (stored.error() != database::result_code::not_resolved) {
            spdlog::error("[utxo_builder] capture_block_undo: reading a spent output failed at "
                "height {}", height);
            return std::unexpected(stored.error());
        }

        deferred.emplace_back(key, height);
    }

    if ( ! deferred.empty()) {
        // This capture's OWN batch. resolve() borrows the span and keeps
        // nothing, so no other component can consume these and none of theirs
        // can arrive here.
        auto resolved = source.utxo_resolve_raw(deferred);
        if ( ! resolved) {
            // The resolution did not run, so nothing below can distinguish
            // "spent output absent" from "never looked". Capturing an undo
            // record on that basis would write a reorg record that cannot be
            // trusted.
            spdlog::error("[utxo_builder] capture_block_undo: resolving {} spent output(s) "
                "failed at height {}; no undo record is captured", deferred.size(), height);
            return std::unexpected(resolved.error());
        }

        // Matched BY KEY: the lists are deduplicated over distinct keys and are
        // not parallel to the request span.
        for (auto const& request : deferred) {
            auto it = resolved->found.find(request.key);
            if (it == resolved->found.end()) {
                // The delta says this output is spent and the set does not have
                // it. What that means depends on what the caller already knows
                // about the block — this function does not know, and does not
                // guess; it reports the fact and leaves the reading to whoever
                // called (see the UTXO build, which knows the block validated).
                spdlog::error("[utxo_builder] capture_block_undo: {} is spent by this block and "
                    "is proven absent from the UTXO set at height {}",
                    utxoz::outpoint_to_string(request.key), height);
                return std::unexpected(database::result_code::key_not_found);
            }
            undo.spent.push_back(database::spent_output{
                request.key, it->second.value, it->second.height});
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

// Forward declaration to avoid circular include
struct block_chain;

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
