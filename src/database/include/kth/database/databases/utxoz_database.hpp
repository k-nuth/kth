// Copyright (c) 2016-2025 Knuth Project developers.
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

namespace kth::database {

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

    /// Insert a UTXO
    /// @param point Output point (txid + index)
    /// @param entry UTXO entry data
    /// @return result_code::success on success
    [[nodiscard]]
    result_code insert(domain::chain::point const& point, utxo_entry const& entry);

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
    // Batch Operations (for UTXO set building)
    // =============================================================================

    /// Apply a batch of UTXO changes
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
            if ( ! db_->insert(key, value, entry.height())) {
                spdlog::error("[utxoz_database] Failed to insert UTXO from block {} - already exists: {}:{}",
                    entry.height(), encode_hash(point.hash()), point.index());
                return result_code::duplicated_key;
            }
        }

        for (auto const& [point, height] : deletes) {
            auto key = point_to_key(point);
            std::ignore = db_->erase(key, height);
        }

        return result_code::success;
    }

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
            // Bloom filter skip moved to process_compact_block_utxos (earlier in pipeline)
            // if (utxo_bloom_ && ! utxo_bloom_->may_contain(key)) {
            //     continue;
            // }
            try {
                if ( ! db_->insert(key, raw.data, raw.height)) {
                    spdlog::error("[utxoz_database] Duplicate key at block {}, outpoint={}, value_size={}",
                        raw.height, utxoz::outpoint_to_string(key), raw.data.size());
                    return result_code::duplicated_key;
                }
            } catch (std::out_of_range const& e) {
                spdlog::error("[utxoz_database] Value too large ({} bytes) at block {}, outpoint={} — skipping",
                    raw.data.size(), raw.height, utxoz::outpoint_to_string(key));
            }
        }

        for (auto const& [key, height] : deletes) {
            // Bloom filter skip moved to process_compact_block_utxos (earlier in pipeline)
            // if (utxo_bloom_ && ! utxo_bloom_->may_contain(key)) {
            //     continue;
            // }
            std::ignore = db_->erase(key, height);
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
    /// @return pair of (successful deletions, failed entries with key and height)
    [[nodiscard]]
    std::pair<size_t, std::vector<utxoz::deferred_deletion_entry>> process_pending_deletions();

    /// Compact the database
    void compact();

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

    std::unique_ptr<utxoz::db> db_;
    bool is_open_ = false;
    std::shared_ptr<utxo_bloom_filter const> utxo_bloom_;  // optional bloom filter for skip-insert optimization
};

} // namespace kth::database

#endif // KTH_DATABASE_UTXOZ_DATABASE_HPP_
