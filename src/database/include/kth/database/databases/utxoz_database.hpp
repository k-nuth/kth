// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_UTXOZ_DATABASE_HPP_
#define KTH_DATABASE_UTXOZ_DATABASE_HPP_

#include <concepts>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <span>

#include <boost/bloom/filter.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <spdlog/spdlog.h>

#include <utxoz/utxoz.hpp>

#include <kth/database/define.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

#ifdef KTH_UTXOZ_REFERENCE_MODE
#include <kth/database/flat_file_pos.hpp>
#include <kth/database/header_index.hpp>
#endif

namespace kth::database {

#ifdef KTH_UTXOZ_REFERENCE_MODE
struct block_store;
#endif

/// What a successful sync() is worth on this platform.
///
/// New in UTXO-Z 0.9.0, and worth asking rather than assuming: under
/// `contents_only` the file contents reach the disk and the directory entries
/// naming them do not, so a checkpoint recorded on the strength of a successful
/// sync is weaker than it looks. Constant for the build, and safe to branch on.
[[nodiscard]]
KD_API utxoz::durability_level utxoz_platform_durability();

// Bloom filter type for UTXO skip-insert optimization.
// K=7 hash functions, default subfilter/stride, uses std::hash<raw_outpoint>.
using utxo_bloom_filter = boost::bloom::filter<utxoz::raw_outpoint, 7>;

// Concepts for UTXO delta maps (accept any hasher/allocator)
template <typename T>
concept utxo_insert_range = std::ranges::range<T> && requires(std::ranges::range_value_t<T> v) {
    { v.first } -> std::convertible_to<domain::chain::point const&>;
    { v.second } -> std::convertible_to<utxo_entry const&>;
};

template <typename T>
concept utxo_delete_range = std::ranges::range<T> && requires(std::ranges::range_value_t<T> v) {
    { v.first } -> std::convertible_to<domain::chain::point const&>;
    { v.second } -> std::convertible_to<uint32_t>;
};

// =============================================================================
// UTXO-Z Database Adapter
// =============================================================================
// Provides a high-performance UTXO database using UTXO-Z library.
// Converts between Knuth types and UTXO-Z types.
// =============================================================================

struct KD_API utxoz_database {
    utxoz_database() = default;
    ~utxoz_database();

    // Non-copyable
    utxoz_database(utxoz_database const&) = delete;
    utxoz_database& operator=(utxoz_database const&) = delete;

    // Movable
    utxoz_database(utxoz_database&&) noexcept = default;
    utxoz_database& operator=(utxoz_database&&) noexcept = default;

    // =============================================================================
    // Configuration
    // =============================================================================

    /// Configure and open the database
    /// @param path Database directory path
    /// @param remove_existing If true, remove existing database files
    /// @return true on success, false on failure
    [[nodiscard]]
    bool open(std::filesystem::path const& path, bool remove_existing = false);

    /// Close the database
    void close();

    /// Check if the database is open
    [[nodiscard]]
    bool is_open() const;

    // =============================================================================
    // UTXO Operations
    // =============================================================================

    /// Get the total number of UTXOs in the database
    [[nodiscard]]
    size_t size() const;

#ifndef KTH_UTXOZ_REFERENCE_MODE
    /// Insert a UTXO (full mode only — reference mode uses apply_delta_raw)
    /// @param point Output point (txid + index)
    /// @param entry UTXO entry data
    /// @return result_code::success on success
    [[nodiscard]]
    result_code insert(domain::chain::point const& point, utxo_entry const& entry);
#endif

    /// Find a UTXO by output point
    /// @param point Output point to search for
    /// @param height Current block height (for statistics)
    /// @return UTXO entry if found, error code otherwise
    [[nodiscard]]
    std::expected<utxo_entry, result_code> find(domain::chain::point const& point, uint32_t height = 0) const;

    /// Erase a UTXO by output point
    /// @param point Output point to erase
    /// @param height Current block height
    /// @return result_code::success if erased, result_code::not_found if not found
    [[nodiscard]]
    result_code erase(domain::chain::point const& point, uint32_t height = 0);

    // =============================================================================
    // Raw access (reorg undo capture / restore)
    // =============================================================================

    /// A UTXO exactly as stored, without resolving it into a utxo_entry.
    /// `value` is the storage-native payload — in reference mode the 8-byte
    /// {file_number, tx_offset} reference, in full mode the serialized entry — so
    /// it can be fed straight back into apply_delta_raw's insert range. `height` is
    /// the entry's ORIGINAL creation height, which restoring must preserve.
    struct raw_stored {
        std::vector<uint8_t> value;
        uint32_t height;
    };

    /// Read a UTXO's stored payload without resolving it (reference mode does not
    /// touch the flat block files here; only resolve/find does).
    ///
    /// Two-phase contract, same as find(): a key_not_found result only means "not
    /// in the active version file, queued for the deferred sweep" — it is NOT proof
    /// of absence. Drain the queue with process_pending_lookups_raw() before
    /// concluding a UTXO is missing.
    ///
    /// @param key Raw outpoint key.
    /// @param height Access height (statistics only; does not affect the result).
    [[nodiscard]]
    std::expected<raw_stored, result_code> find_raw(utxoz::raw_outpoint const& key, uint32_t height = 0) const;

    /// Resolve the deferred-lookup queue, returning payloads verbatim (no
    /// utxo_entry reconstruction — the cheap counterpart of
    /// process_pending_lookups()).
    /// @return on success, (resolved entries keyed by outpoint, keys PROVEN
    ///         absent — see process_pending_lookups()). On failure, the error: a
    ///         queue that could not be drained is NOT a queue that resolved to
    ///         nothing, and callers read "not resolved" as "this UTXO is spent".
    [[nodiscard]]
    std::expected<std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, raw_stored>,
                            std::vector<utxoz::raw_outpoint>>, result_code>
    process_pending_lookups_raw();

    // =============================================================================
    // Batch Operations (for UTXO set building)
    // =============================================================================

#ifndef KTH_UTXOZ_REFERENCE_MODE
    /// Apply a batch of UTXO changes (full mode only — reference mode uses apply_delta_raw)
    /// @param inserts UTXOs to add (point -> utxo_entry, entry contains height)
    /// @param deletes UTXOs to remove (point -> height for traceability)
    /// @return result_code::success on success
    template <utxo_insert_range Inserts, utxo_delete_range Deletes>
    [[nodiscard]]
    result_code apply_delta(Inserts const& inserts, Deletes const& deletes) {
        if ( ! is_open()) {
            return result_code::other;
        }

        for (auto const& [point, entry] : inserts) {
            auto key = point_to_key(point);
            auto value = entry_to_bytes(entry);
            auto ins = db_->insert(key, value, entry.height());
            if ( ! ins || ! *ins) {
                spdlog::error("[utxoz_database] Failed to insert UTXO from block {} - {}:{}",
                    entry.height(), encode_hash(point.hash()), point.index());
                return ins ? result_code::duplicated_key : result_code::other;
            }
        }

        for (auto const& [point, height] : deletes) {
            auto key = point_to_key(point);
            auto const erased = db_->erase(key, height);
            if ( ! erased && erased.error() != utxoz::error_code::not_found) {
                // A delete that failed leaves a spent output in the set. Ignoring
                // it and returning success let a batch be recorded as applied
                // while the UTXO it spent is still there. not_found is different:
                // the key was not present, which a delta can legitimately hit.
                spdlog::error("[utxoz_database] Failed to erase UTXO from block {} - {}:{}",
                    height, encode_hash(point.hash()), point.index());
                return result_code::other;
            }
        }

        return result_code::success;
    }
#endif

    /// Apply a batch of raw UTXO changes (zero-copy path).
    /// Keys are raw_outpoint (36 bytes) — no conversion needed.
    /// Inserts: range of {raw_outpoint, {data, height}}.
    /// Deletes: range of {raw_outpoint, height}.
    template <typename Inserts, typename Deletes>
    [[nodiscard]]
    result_code apply_delta_raw(Inserts const& inserts, Deletes const& deletes) {
        if ( ! is_open()) {
            return result_code::other;
        }

        for (auto const& [key, raw] : inserts) {
#ifdef KTH_UTXOZ_REFERENCE_MODE
            // Reference mode: deserialize the 8-byte reference into typed fields.
            // Checked first: these are two reads off a caller-supplied buffer,
            // and a short payload would read past its end. A payload that is not
            // exactly eight bytes is not a reference at all, whatever it is.
            if (raw.data.size() != 8) {
                spdlog::error("[utxoz_database] reference payload for {} is {} byte(s), "
                    "not 8; refusing to read it as a reference",
                    utxoz::outpoint_to_string(key), raw.data.size());
                return result_code::other;
            }
            uint32_t file_number, tx_offset;
            std::memcpy(&file_number, raw.data.data(), 4);
            std::memcpy(&tx_offset, raw.data.data() + 4, 4);
            auto ins = db_->insert(key, file_number, tx_offset, raw.height);
#else
            auto ins = db_->insert(key, raw.data, raw.height);
#endif
            if ( ! ins) {
                spdlog::error("[utxoz_database] Insert error at block {}, outpoint={}",
                    raw.height, utxoz::outpoint_to_string(key));
                return result_code::other;
            }
            if ( ! *ins) {
                spdlog::error("[utxoz_database] Duplicate key at block {}, outpoint={}",
                    raw.height, utxoz::outpoint_to_string(key));
                return result_code::duplicated_key;
            }
        }

        for (auto const& [key, height] : deletes) {
            auto const erased = db_->erase(key, height);
            if ( ! erased && erased.error() != utxoz::error_code::not_found) {
                spdlog::error("[utxoz_database] Failed to erase {} at block {}",
                    utxoz::outpoint_to_string(key), height);
                return result_code::other;
            }
        }

        return result_code::success;
    }

    /// Iterate over all UTXO keys in the database.
    /// @param callback Callable with signature void(utxoz::raw_outpoint const&)
    template <typename F>
    void for_each_utxo(F&& callback) const {
        if (is_open()) {
            db_->for_each_key(std::forward<F>(callback));
        }
    }

    /// Set the bloom filter for skip-insert optimization during IBD.
    /// When set, apply_delta_raw will skip inserts/deletes for keys not in the filter.
    void set_utxo_bloom(std::shared_ptr<utxo_bloom_filter const> bloom) {
        utxo_bloom_ = std::move(bloom);
    }

    /// Clear the bloom filter (disable skip-insert optimization).
    void clear_utxo_bloom() {
        utxo_bloom_.reset();
    }

#ifdef KTH_UTXOZ_REFERENCE_MODE
    /// Set the block store for reference mode find resolution.
    void set_block_store(block_store const* store) { block_store_ = store; }

    /// Set the header index for reference mode find resolution (MTP calculation).
    void set_header_index(header_index const* idx) { header_index_ = idx; }
#endif

    /// Clear all UTXOs from the database
    /// @return result_code::success on success
    [[nodiscard]]
    result_code clear();

    // =============================================================================
    // Maintenance
    // =============================================================================

    /// Get the number of pending deferred deletions
    [[nodiscard]]
    size_t deferred_deletions_size() const;

    /// Process pending deferred deletions
    /// @return on success, (successful deletions, failed entries with key and
    ///         height). On failure, the error: no deletion was applied, which is
    ///         not the same as there having been none to apply.
    [[nodiscard]]
    std::expected<std::pair<uint32_t, std::vector<utxoz::deferred_deletion_entry>>, result_code>
    process_pending_deletions();

    /// Get the number of pending deferred lookups
    [[nodiscard]]
    size_t deferred_lookups_size() const;

    /// Process pending deferred lookups (sweeps cached/older file versions).
    /// find() is a two-phase contract: its key_not_found only means "not in the
    /// active file, queued". This resolves the queue. Call once per batch, BEFORE
    /// process_pending_deletions().
    /// @return on success, (resolved entries keyed by outpoint, keys PROVEN
    ///         absent). Since UTXO-Z 0.9.1 the second list means exactly that:
    ///         a version file that could not be read makes the whole sweep fail
    ///         with version_unreadable instead of dropping its keys in there. On
    ///         failure, the error — absence is read upstream as "spent".
    [[nodiscard]]
    std::expected<std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, utxo_entry>,
                            std::vector<utxoz::raw_outpoint>>, result_code>
    process_pending_lookups();

    /// Compact the database.
    ///
    /// The name is the compaction OPERATION and did not change when UTXO-Z
    /// renamed its storage mode; what changed is the contract. In 0.8.0
    /// `compact_all()` returned void, so there was nothing to ignore. In 0.9.0
    /// it returns `result<>`, and a caller that keeps calling it as a statement
    /// still compiles while dropping every failure on the floor — which is why
    /// this reports rather than returns void.
    /// @return true if the database compacted, false if it is closed or the
    ///         operation failed (the reason is logged).
    bool compact();

    /// Ask for the data written so far to reach stable storage.
    ///
    /// New in UTXO-Z 0.9.0. Closing does NOT sync: `close()` releases the
    /// mapping and stops, so a caller that wants the guarantee has to ask for
    /// it here first. What the answer is worth depends on the platform —
    /// see platform_durability().
    /// @return true if a barrier was reached; false if the database is closed,
    ///         the platform has none, or the barrier failed (each is logged as
    ///         the distinct thing it is).
    bool sync();

    /// Print statistics to log
    void print_statistics();

    /// Print sizing report to log
    void print_sizing_report();

    /// Print height range stats to log
    void print_height_range_stats();

    // Convert utxoz::raw_outpoint back to domain::chain::point (for error reporting)
    [[nodiscard]]
    static domain::chain::point key_to_point(utxoz::raw_outpoint const& key);

private:
    // Convert domain::chain::point to utxoz::raw_outpoint
    [[nodiscard]]
    static utxoz::raw_outpoint point_to_key(domain::chain::point const& point);

    // Serialize utxo_entry to bytes
    [[nodiscard]]
    static std::vector<uint8_t> entry_to_bytes(utxo_entry const& entry);

    // Deserialize bytes to utxo_entry
    [[nodiscard]]
    static std::expected<utxo_entry, result_code> bytes_to_entry(std::span<uint8_t const> bytes);

    // Resolve a reference find result to a full utxo_entry.
    // Used in reference mode find path.
#ifdef KTH_UTXOZ_REFERENCE_MODE
    [[nodiscard]]
    std::expected<utxo_entry, result_code> resolve_reference_ref(
        utxoz::reference_find_result const& ref,
        uint32_t output_index) const;
#endif

#ifdef KTH_UTXOZ_REFERENCE_MODE
    std::optional<utxoz::reference_db> db_;
#else
    std::optional<utxoz::full_db> db_;
#endif
    bool is_open_ = false;
    std::shared_ptr<utxo_bloom_filter const> utxo_bloom_;  // optional bloom filter for skip-insert optimization

#ifdef KTH_UTXOZ_REFERENCE_MODE
    block_store const* block_store_ = nullptr;
    header_index const* header_index_ = nullptr;
#endif
};

} // namespace kth::database

#endif // KTH_DATABASE_UTXOZ_DATABASE_HPP_
