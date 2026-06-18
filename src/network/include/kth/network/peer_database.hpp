// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_DATABASE_HPP
#define KTH_NETWORK_PEER_DATABASE_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <boost/unordered/concurrent_flat_map.hpp>

#include <asio/ip/address.hpp>
#include <simdjson.h>

#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/normalized_address.hpp>
#include <kth/network/peer_record.hpp>

namespace kth::network {

// =============================================================================
// Peer Database - Unified storage for all peer information
// =============================================================================
//
// Thread-safe database of peer_records using boost::concurrent_flat_map.
// Replaces separate banlist.dat and hosts.cache files.
//
// Key features:
// - Thread-safe (lock-free concurrent access)
// - Persistence to single file (peers.dat)
// - Reputation tracking with decay
// - Performance statistics
// - Ban management
//
// =============================================================================

/// Result of get_or_create operation
enum class get_result {
    existing,           ///< Record already existed in database
    created,            ///< Record was created and inserted
    created_not_stored  ///< Record was created but NOT inserted (capacity full)
};

struct KN_API peer_database {
    using clock = std::chrono::system_clock;
    static constexpr size_t default_capacity = 10000;

    /// Construct database with optional persistence file
    explicit
    peer_database(kth::path file_path = {}, size_t capacity = default_capacity);

    /// Destructor - saves to file
    ~peer_database();

    // Non-copyable
    peer_database(peer_database const&) = delete;
    peer_database& operator=(peer_database const&) = delete;

    // -------------------------------------------------------------------------
    // Basic Operations
    // -------------------------------------------------------------------------

    /// Get or create a record for an address
    /// Returns the record and the result indicating what happened
    [[nodiscard]]
    std::pair<peer_record, get_result> get_or_create(infrastructure::config::authority const& auth);

    /// Create a new record for an address (does not insert into database)
    /// Sets first_seen = last_seen = now
    [[nodiscard]]
    peer_record create_record(infrastructure::config::authority const& auth);

    /// Update an existing record. Returns true if record was found and updated.
    /// Does NOT insert if record doesn't exist (preserves capacity invariant).
    [[nodiscard]]
    bool update(peer_record const& record);

    /// Get a record if it exists
    [[nodiscard]]
    std::optional<peer_record> get(infrastructure::config::authority const& auth) const;

    /// Get a record by IP (ignores port)
    [[nodiscard]]
    std::optional<peer_record> get_by_ip(::asio::ip::address const& ip) const;

    /// Check if an address exists
    [[nodiscard]]
    bool exists(infrastructure::config::authority const& auth) const;

    /// Remove a record
    [[nodiscard]]
    bool remove(infrastructure::config::authority const& auth);

    /// Get total number of records
    [[nodiscard]]
    size_t size() const;

    /// Get count of available and banned peers in one pass
    /// Returns {available, banned}
    [[nodiscard]]
    std::pair<size_t, size_t> count_by_status() const;

    /// Clear all records
    void clear();

    /// Store a network address (from seeding or addr messages)
    /// Creates or updates the record, returns true if new
    [[nodiscard]]
    bool store_address(domain::message::network_address const& addr);

    /// Fetch a random non-banned address for connection
    /// Returns error code if no addresses available
    [[nodiscard]]
    code fetch_address(domain::message::network_address& out) const;

    /// Remove an address
    [[nodiscard]]
    bool remove_address(domain::message::network_address const& addr);

    // -------------------------------------------------------------------------
    // Ban Operations
    // -------------------------------------------------------------------------

    /// Check if an IP is banned
    [[nodiscard]]
    bool is_banned(::asio::ip::address const& ip) const;

    /// Check if an authority is banned
    [[nodiscard]]
    bool is_banned(infrastructure::config::authority const& auth) const;

    /// Ban an IP (updates or creates record)
    void ban(::asio::ip::address const& ip,
             std::chrono::seconds duration,
             ban_reason reason = ban_reason::node_misbehaving);

    /// Ban an authority
    void ban(infrastructure::config::authority const& auth,
             std::chrono::seconds duration,
             ban_reason reason = ban_reason::node_misbehaving);

    /// Unban an IP
    [[nodiscard]]
    bool unban(::asio::ip::address const& ip);

    /// Unban an authority
    [[nodiscard]]
    bool unban(infrastructure::config::authority const& auth);

    /// Get all banned peers
    [[nodiscard]]
    std::vector<peer_record> get_banned() const;

    /// Remove expired bans
    void sweep_expired_bans();

    // -------------------------------------------------------------------------
    // Reputation Operations
    // -------------------------------------------------------------------------

    /// Add misbehavior score to a peer, returns true if should be banned
    [[nodiscard]]
    bool add_misbehavior(infrastructure::config::authority const& auth,
                                       int score,
                                       int ban_threshold = 100);

    /// Decay reputation for all peers (call periodically)
    void decay_all_reputation(int amount = 1);

    // -------------------------------------------------------------------------
    // Performance Operations
    // -------------------------------------------------------------------------

    /// Record block download performance
    void record_block_download(infrastructure::config::authority const& auth,
                               uint32_t blocks,
                               uint32_t time_ms);

    /// Get average performance across all peers with samples
    [[nodiscard]]
    double get_median_performance() const;

    /// Check if a peer is slow based on download performance
    /// Returns true if peer has enough samples and avg_ms_per_block > threshold
    /// @param auth The peer's authority
    /// @param threshold_ms Threshold in ms/block (default 500ms, ~2x typical good peer)
    /// @param min_samples Minimum samples before marking slow (default 3)
    [[nodiscard]]
    bool is_slow_peer(infrastructure::config::authority const& auth,
                      double threshold_ms = 500.0,
                      uint32_t min_samples = 3) const;

    /// Get peer's average download speed (ms per block)
    /// Returns 0.0 if peer not found or no samples
    [[nodiscard]]
    double get_peer_speed(infrastructure::config::authority const& auth) const;

    // -------------------------------------------------------------------------
    // Query Operations
    // -------------------------------------------------------------------------

    /// Get addresses suitable for connection (not banned, good reputation)
    [[nodiscard]]
    std::vector<infrastructure::config::authority> get_connectable(size_t max_count) const;

    /// Get addresses to avoid (banned or bad reputation)
    [[nodiscard]]
    std::vector<infrastructure::config::authority> get_bad_peers() const;

    /// Visit all records (read-only)
    void visit_all(std::function<void(peer_record const&)> visitor) const;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// Load from file
    [[nodiscard]]
    bool load();

    /// Save to file
    [[nodiscard]]
    bool save() const;

    /// Import from legacy hosts.cache file
    [[nodiscard]]
    size_t import_hosts_cache(kth::path const& file_path);

    /// Import from legacy banlist.dat file
    [[nodiscard]]
    size_t import_banlist(kth::path const& file_path);

private:
    /// Parse a single JSON line from peers.dat using simdjson
    std::optional<peer_record> parse_line(std::string_view line, simdjson::ondemand::parser& parser) const;

    /// Format a record for persistence
    std::string format_record(peer_record const& record) const;

    // Key type for the map (normalized IP address, port ignored for banning)
    // But we store full authority in the record
    // Note: salted_ip_hasher works with normalized_address via implicit conversion to asio::ip::address
    boost::concurrent_flat_map<normalized_address, peer_record, kth::salted_ip_hasher> records_;
    kth::path file_path_;
    size_t capacity_;
};

} // namespace kth::network

#endif // KTH_NETWORK_PEER_DATABASE_HPP
