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
#include <utility>

#include <boost/bloom/filter.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <spdlog/spdlog.h>

#include <utxoz/utxoz.hpp>

#include <kth/database/define.hpp>
#include <kth/database/durability.hpp>
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

/// The name of a UTXO-Z error code, for logs.
///
/// Declared here rather than kept file-local because the templates in this
/// header report failures too, and a diagnosis that reads "error 14" against
/// whichever enum order the linked UTXO-Z happened to have is not one.
[[nodiscard]]
KD_API char const* utxoz_error_name(utxoz::error_code code);

/// Is this the ordinary outcome of probing the active versions?
///
/// In 0.10.0 exactly ONE code is: `not_resolved`, which says the active versions
/// cannot answer and the older ones were not consulted. Every prevout that needs
/// history produces it, so logging it as a failure turns a routine miss into an
/// error line per input — and buries the faults that matter among them.
///
/// A single predicate rather than the condition repeated at each probe, because
/// it was repeated and the two copies disagreed with the library: they still
/// tested `not_found`, which 0.10.0 no longer returns from a probe, so every
/// ordinary miss took the error path.
///
/// NOT a statement about absence. Absence is established by resolving a batch,
/// and this code never means it.
[[nodiscard]]
inline bool is_ordinary_probe_miss(utxoz::error_code code) {
    return code == utxoz::error_code::not_resolved;
}

/// A KTH height narrowed to the type UTXO-Z's request records carry.
///
/// Both `lookup_request` and `deferred_deletion_entry` hold a `uint32_t`, while
/// several KTH signatures carry heights as `size_t` — `populate_prevout` among
/// them. Letting that narrow implicitly is a hard error on Apple Clang and a
/// SILENT TRUNCATION everywhere else, and a truncated height is not a smaller
/// height: it names a different block. The resolution would then be bounded at
/// the wrong point, which is a wrong answer rather than a failed one.
///
/// So it is checked and reported. A caller that gets `nullopt` has a height this
/// store cannot express, which is an operational failure of this node and must
/// never be reported as the output being absent.
///
/// @return the narrowed height, or nullopt if it does not fit.
[[nodiscard]]
inline std::optional<uint32_t> to_store_height(size_t height) {
    if ( ! std::in_range<uint32_t>(height)) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(height);
}

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

    /// What resolving a batch of typed lookups answered. Matched back BY KEY —
    /// deduplicated over distinct keys, not positional against the span.
    struct entry_resolution {
        boost::unordered_flat_map<utxoz::raw_outpoint, utxo_entry> found;
        std::vector<utxoz::raw_outpoint> absent;   ///< PROVEN absent, and only that
    };

    /// Resolve a batch of lookups the CALLER owns, materialising utxo_entry.
    ///
    /// Same contract as resolve_raw(): probe with find() first, since this walks
    /// the versions BELOW the active ones; `absent` is proven absence and never
    /// stands for a storage fault; an error returns no lists and consumes
    /// nothing, so the same span may be retried.
    [[nodiscard]]
    std::expected<entry_resolution, result_code>
    resolve(std::span<utxoz::lookup_request const> requests) const;


    // =============================================================================
    // Raw access (reorg undo capture / restore)
    // =============================================================================

    /// A UTXO exactly as stored, without resolving it into a utxo_entry.
    /// `value` is the storage-native payload — in reference mode the 8-byte
    /// {file_number, tx_offset} reference, in full mode the serialized entry —
    /// which is the same shape apply_delta_raw inserts. It is NOT the same type:
    /// that range holds utxo_raw_value, whose payload member is `data`, so a
    /// range of these does not compile there and the caller rebuilds the pair
    /// (see disconnect_block). `height` is the entry's ORIGINAL creation height,
    /// which restoring must preserve.
    struct raw_stored {
        std::vector<uint8_t> value;
        uint32_t height;
    };

    /// Probe the ACTIVE versions for a UTXO's stored payload, without resolving
    /// it (reference mode does not touch the flat block files here).
    ///
    /// Two-phase, and the store now keeps nothing between the phases: a
    /// `not_resolved` result means the active versions cannot answer, NOT that
    /// the key is absent, and nothing has been queued on the caller's behalf.
    /// Keeping the request is the caller's job — collect the misses and hand
    /// them to resolve_raw() as a batch it owns.
    ///
    /// @param key Raw outpoint key.
    /// @param height Access height (statistics only; does not affect the result).
    [[nodiscard]]
    std::expected<raw_stored, result_code> find_raw(utxoz::raw_outpoint const& key, uint32_t height = 0) const;

    /// What resolving a batch of raw lookups answered.
    ///
    /// One entry per DISTINCT key of the request span, never one per request:
    /// UTXO-Z deduplicates by key keeping the first occurrence, so these are
    /// matched back BY KEY. Never by position against the span, and never by
    /// count — `found.size() + absent.size()` is the number of distinct keys.
    struct raw_resolution {
        boost::unordered_flat_map<utxoz::raw_outpoint, raw_stored> found;
        std::vector<utxoz::raw_outpoint> absent;   ///< PROVEN absent, and only that
    };

    /// Resolve a batch of lookups the CALLER owns, against the older versions.
    ///
    /// The batch is borrowed for the call and nothing survives it, so two
    /// components can each resolve their own without agreeing which of them is
    /// allowed to, and neither can consume the other's request. That is what
    /// replaced the global queue.
    ///
    /// Probe with find_raw() FIRST. This walks the cached files and the versions
    /// BELOW the current one — not the active ones — so a key that lives in the
    /// active version and was never probed comes back in `absent`, which would
    /// be a false negative rather than a fact.
    ///
    /// @return on success, the resolution. On failure the error, and NO lists at
    ///         all: version_unreadable / catalog_unreadable are storage faults,
    ///         and a caller reading them as absence turns one into a UTXO it
    ///         believes is spent. Nothing was consumed, so the same span can be
    ///         retried once the fault is dealt with.
    [[nodiscard]]
    std::expected<raw_resolution, result_code>
    resolve_raw(std::span<utxoz::lookup_request const> requests) const;

    // =============================================================================
    // Batch Operations (for UTXO set building)
    // =============================================================================

#ifndef KTH_UTXOZ_REFERENCE_MODE
    /// Apply a batch of UTXO insertions (full mode only — reference mode uses
    /// apply_inserts_raw).
    ///
    /// Insertions ONLY. Deletions are a separate call now, because they are a
    /// separate contract: apply_deletes() returns a three-way partition the
    /// caller has to act on, and folding it into a single result_code is what
    /// let a deferred deletion pass as applied.
    ///
    /// @param inserts UTXOs to add (point -> utxo_entry, entry contains height)
    /// @return result_code::success on success
    template <utxo_insert_range Inserts>
    [[nodiscard]]
    result_code apply_inserts(Inserts const& inserts) {
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

        return result_code::success;
    }
#endif

    /// Apply a batch of raw UTXO insertions (zero-copy path).
    /// Keys are raw_outpoint (36 bytes) — no conversion needed.
    /// Inserts: range of {raw_outpoint, {data, height}}.
    ///
    /// Insertions ONLY; see apply_deletes() for the other half and why they no
    /// longer share a call.
    template <typename Inserts>
    [[nodiscard]]
    result_code apply_inserts_raw(Inserts const& inserts) {
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

        return result_code::success;
    }

    /// Iterate over all UTXO keys in the database.
    ///
    /// @param callback Callable with signature void(utxoz::raw_outpoint const&)
    /// @return false if the store is closed or the walk could not read every
    ///         version file it needed. A partial walk is NOT reported through
    ///         the callback — it simply stops — so a caller that discards this
    ///         cannot tell a complete set from a truncated one. What is built
    ///         from one of these is a filter that decides which keys
    ///         apply_delta_raw is allowed to skip, and a key missing from it is
    ///         a delete that never happens.
    template <typename F>
    [[nodiscard]]
    bool for_each_utxo(F&& callback) const {
        if ( ! is_open()) {
            return false;
        }
        auto const walked = db_->for_each_key(std::forward<F>(callback));
        if ( ! walked) {
            spdlog::error("[utxoz_database] Walking the UTXO set failed: {}; the walk stopped "
                "where it failed and what it visited is a subset, not the set",
                utxoz_error_name(walked.error()));
            return false;
        }
        return true;
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

    /// Apply a batch of deletions the CALLER owns.
    ///
    /// The one mutating entry point for removal. Each request is tried against
    /// the active versions, then the cached files, then every version below the
    /// current one, with keys dropped from the working set as they are applied —
    /// so the descent is paid once per batch rather than once per key. That is
    /// why there is no single-key erase() any more: without a queue inside the
    /// store, a lone key would have to pay the whole descent by itself.
    ///
    /// @warning Not transactional, and it cannot be: it writes as it goes, so a
    /// fault partway through leaves earlier deletions APPLIED. That is why this
    /// returns progress rather than an error — the applied part is enumerated
    /// exactly, in `erased`, including on the failure path, and must never be
    /// resent.
    ///
    /// @warning Mutating, so it needs a window with no find(), resolve(),
    /// insert(), compaction or close() in flight. UTXO-Z's internal lock covers
    /// resolve-vs-resolve ONLY and does not extend here.
    ///
    /// THE CALLER MUST HOLD A `blockchain::utxo_write_window` (#649). That is
    /// the window: it excludes every find(), resolve(), insert(), compaction and
    /// close() over this store for as long as it is held, and it is a capability
    /// rather than a convention — `blockchain::guarded_store` will not hand out
    /// the database without one, so reaching this method without the exclusion
    /// does not compile on that side.
    ///
    /// It must span the whole logical mutation and not this call, because a
    /// reader admitted between the inserts and the deletions sees a set still
    /// holding outputs the blocks spent. What the window prevents here is
    /// concrete: a deletion writing through the cache's mappings while a probe
    /// reads is a use-after-unmap — a crash, not a wrong answer — and the
    /// single-transaction validate path probes lock-free, reachable over
    /// JSON-RPC and the C API.
    ///
    /// @return erased / absent / unresolved / error. The partition is over
    ///         DISTINCT keys (deduplicated keeping the first occurrence), so the
    ///         sizes do not add up to requests.size() and results are matched
    ///         back by key, never by position. Resend `unresolved`, and only it.
    [[nodiscard]]
    utxoz::deletion_progress apply_deletes(std::span<utxoz::deferred_deletion_entry const> requests);

    [[nodiscard]]
    bool compact();

    /// Ask for the data written so far to reach stable storage.
    ///
    /// New in UTXO-Z 0.9.0. Closing does NOT sync: `close()` releases the
    /// mapping and stops, so a caller that wants the guarantee has to ask for
    /// it here first. What the answer is worth depends on the platform —
    /// see platform_durability().
    ///
    /// Three answers rather than a bool. `sync_unsupported` and `sync_failed`
    /// are different facts about the machine and only one is a defect: a
    /// caller that has to decide whether the node may continue past this point
    /// cannot get that from "false", and a policy built on "false" would either
    /// stop a node on a platform that never had the barrier or carry one on
    /// past a disk that refused to flush.
    ///
    /// A closed database reports `failed`: there is nothing mapped, so a caller
    /// asking for a guarantee did not get one, and that is not the same as a
    /// platform that has none.
    [[nodiscard]]
    barrier_outcome sync();

    /// Print statistics to log.
    ///
    /// NOT const, and not an observation: UTXO-Z recomputes the fragmentation
    /// counters as it goes, and documents that this must not overlap a mutation
    /// nor another statistics call. It takes the exclusive window for that
    /// reason, unlike the two below.
    void print_statistics();

    /// Print sizing report to log. Const on both sides: a genuine observation.
    void print_sizing_report() const;

    /// Print height range stats to log. Const on both sides.
    void print_height_range_stats() const;

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
