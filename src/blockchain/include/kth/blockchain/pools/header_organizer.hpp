// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain.hpp>
#include <kth/domain/message/header.hpp>

namespace kth::blockchain {

// Forward declaration
struct block_chain;

/// Result of adding headers to the organizer
struct header_organize_result {
    code error;
    size_t headers_added{0};
    size_t cache_size{0};
    size_t cache_memory_bytes{0};
};

/// Configuration for the header cache
struct header_cache_config {
    /// Maximum memory to use for header cache (bytes)
    /// Default: 256 MB
    size_t max_cache_memory = 256 * 1024 * 1024;

    /// Memory overhead factor (for allocator overhead)
    /// Default: 1.12 (~12% overhead for jemalloc)
    double memory_overhead = 1.12;

    /// Auto-flush when cache reaches this percentage of max
    /// Default: 0.9 (90%)
    double auto_flush_threshold = 0.9;
};

/// Header organizer for headers-first sync.
/// Validates and caches headers during initial block download (IBD).
///
/// ARCHITECTURE:
/// -------------
/// The organizer maintains an in-memory cache of validated headers.
/// Headers are validated as they arrive and stored in the cache.
/// When flush() is called (or cache is full), headers are persisted to DB.
///
/// This design allows:
/// 1. Fast header validation without DB round-trips
/// 2. Batch DB writes for efficiency
/// 3. Memory-aware caching with configurable limits
///
/// THREAD SAFETY:
/// --------------
/// All public methods are thread-safe (protected by mutex).
struct KB_API header_organizer {
    using ptr = std::shared_ptr<header_organizer>;

    /// Construct a header organizer.
    /// @param[in] chain     Reference to the blockchain.
    /// @param[in] settings  Blockchain settings.
    /// @param[in] network   The network type.
    /// @param[in] config    Cache configuration.
    header_organizer(block_chain& chain, settings const& settings,
                     domain::config::network network,
                     header_cache_config config = {});

    ~header_organizer();

    // Non-copyable
    header_organizer(header_organizer const&) = delete;
    header_organizer& operator=(header_organizer const&) = delete;

    bool start();
    bool stop();

    /// Initialize the organizer with current chain state.
    /// Must be called before adding headers.
    /// @param[in] current_height     Current header height in DB.
    /// @param[in] current_tip_hash   Hash of current header tip.
    /// @param[in] expected_headers   Estimated number of headers to sync (for pre-allocation).
    void initialize(size_t current_height, hash_digest const& current_tip_hash,
                    size_t expected_headers = 0);

    /// Add a batch of headers to the organizer.
    /// Validates all headers and adds valid ones to the cache.
    /// May auto-flush to DB if cache is getting full.
    /// @param[in] headers  The headers to add.
    /// @return Result with error code and statistics.
    [[nodiscard]]
    header_organize_result add_headers(domain::message::header::list const& headers);

    /// Flush all cached headers to the database.
    /// @return error::success or the storage error.
    [[nodiscard]]
    code flush();

    /// Get the block hashes of all cached headers.
    /// Useful for requesting blocks after headers are synced.
    [[nodiscard]]
    std::vector<hash_digest> get_cached_hashes() const;

    /// Clear the cache without flushing to DB.
    /// Use with caution - discards unwritten headers.
    void clear_cache();

    // =========================================================================
    // State queries
    // =========================================================================

    /// Get the current header tip height (DB + cache).
    [[nodiscard]]
    size_t header_height() const;

    /// Get the hash of the header tip.
    [[nodiscard]]
    hash_digest header_tip_hash() const;

    /// Get the number of headers in cache.
    [[nodiscard]]
    size_t cache_size() const;

    /// Get estimated memory usage of the cache.
    [[nodiscard]]
    size_t cache_memory() const;

    /// Check if cache has pending headers.
    [[nodiscard]]
    bool has_pending() const;

    /// Calculate optimal batch size based on available memory.
    /// @param[in] items_needed  Total headers we want to sync.
    /// @return Optimal number of headers to cache.
    [[nodiscard]]
    size_t calculate_optimal_cache_size(size_t items_needed) const;

protected:
    [[nodiscard]]
    bool stopped() const {
        return stopped_;
    }

private:
    // Validate a single header against expected previous
    [[nodiscard]]
    code validate(domain::chain::header const& header, size_t height,
                  hash_digest const& previous) const;

    // Estimate memory for a single header
    [[nodiscard]]
    static size_t estimated_header_size();

    // Internal cache_memory without lock (caller must hold mutex_)
    [[nodiscard]]
    size_t cache_memory_impl() const;

    // Internal flush without lock (caller must hold mutex_)
    [[nodiscard]]
    code flush_impl();

    // Check if we should auto-flush
    [[nodiscard]]
    bool should_auto_flush() const;

    // Members
    block_chain& chain_;
    std::atomic<bool> stopped_{false};
    validate_header validator_;
    header_cache_config config_;
    mutable std::mutex mutex_;

    // Chain state (protected by mutex_)
    size_t db_header_height_{0};       // Height of headers in DB
    hash_digest db_tip_hash_{null_hash};  // Hash of DB header tip

    // Cache state (protected by mutex_)
    domain::chain::header::list header_cache_;
    std::vector<hash_digest> hash_cache_;  // Parallel cache of hashes
    hash_digest cache_tip_hash_{null_hash};
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP
