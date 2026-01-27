// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_UTXOZ_DATABASE_HPP_
#define KTH_DATABASE_UTXOZ_DATABASE_HPP_

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <utxoz/utxoz.hpp>

#include <kth/database/define.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/domain/chain/point.hpp>

namespace kth::database {

// =============================================================================
// UTXO-Z Database Adapter
// =============================================================================
// Provides a high-performance UTXO database using UTXO-Z library.
// Converts between Knuth types and UTXO-Z types.
// =============================================================================

class KD_API utxoz_database {
public:
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
    [[nodiscard]]
    result_code apply_delta(
        boost::unordered_flat_map<domain::chain::point, utxo_entry> const& inserts,
        boost::unordered_flat_map<domain::chain::point, uint32_t> const& deletes
    );

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
};

} // namespace kth::database

#endif // KTH_DATABASE_UTXOZ_DATABASE_HPP_
