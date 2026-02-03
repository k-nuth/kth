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

class KN_API peer_database {
public:
    using clock = std::chrono::system_clock;
    static constexpr size_t default_capacity = 10000;

    /// Construct database with optional persistence file
    explicit peer_database(kth::path file_path = {}, size_t capacity = default_capacity);

    /// Destructor - saves to file
    ~peer_database();

    // Non-copyable
    peer_database(peer_database const&) = delete;
    peer_database& operator=(peer_database const&) = delete;

    // -------------------------------------------------------------------------
    // Basic Operations
    // -------------------------------------------------------------------------

    /// Get or create a record for an address
    /// If the address doesn't exist, creates a new record with first_seen = now
    peer_record get_or_create(infrastructure::config::authority const& address);

    /// Update a record (must already exist or will be created)
    void update(peer_record const& record);

    /// Get a record if it exists
    std::optional<peer_record> get(infrastructure::config::authority const& address) const;

    /// Get a record by IP (ignores port)
    std::optional<peer_record> get_by_ip(::asio::ip::address const& ip) const;

    /// Check if an address exists
    bool exists(infrastructure::config::authority const& address) const;

    /// Remove a record
    bool remove(infrastructure::config::authority const& address);

    /// Get total number of records
    size_t size() const;

    /// Get count of non-banned peers available for connection
    size_t available_count() const;

    /// Get count of currently banned peers
    size_t banned_count() const;

    /// Clear all records
    void clear();

    /// Store a network address (from seeding or addr messages)
    /// Creates or updates the record, returns true if new
    bool store_address(domain::message::network_address const& addr);

    /// Fetch a random non-banned address for connection
    /// Returns error code if no addresses available
    code fetch_address(domain::message::network_address& out) const;

    /// Remove an address
    bool remove_address(domain::message::network_address const& addr);

    // -------------------------------------------------------------------------
    // Ban Operations
    // -------------------------------------------------------------------------

    /// Check if an IP is banned
    bool is_banned(::asio::ip::address const& ip) const;

    /// Check if an authority is banned
    bool is_banned(infrastructure::config::authority const& address) const;

    /// Ban an IP (updates or creates record)
    void ban(::asio::ip::address const& ip,
             std::chrono::seconds duration,
             ban_reason reason = ban_reason::node_misbehaving);

    /// Ban an authority
    void ban(infrastructure::config::authority const& address,
             std::chrono::seconds duration,
             ban_reason reason = ban_reason::node_misbehaving);

    /// Unban an IP
    bool unban(::asio::ip::address const& ip);

    /// Unban an authority
    bool unban(infrastructure::config::authority const& address);

    /// Get all banned peers
    std::vector<peer_record> get_banned() const;

    /// Remove expired bans
    void sweep_expired_bans();

    // -------------------------------------------------------------------------
    // Reputation Operations
    // -------------------------------------------------------------------------

    /// Add misbehavior score to a peer, returns true if should be banned
    bool add_misbehavior(infrastructure::config::authority const& address,
                         int score,
                         int ban_threshold = 100);

    /// Decay reputation for all peers (call periodically)
    void decay_all_reputation(int amount = 1);

    // -------------------------------------------------------------------------
    // Performance Operations
    // -------------------------------------------------------------------------

    /// Record block download performance
    void record_block_download(infrastructure::config::authority const& address,
                               uint32_t blocks,
                               uint32_t time_ms);

    /// Get average performance across all peers with samples
    double get_median_performance() const;

    // -------------------------------------------------------------------------
    // Query Operations
    // -------------------------------------------------------------------------

    /// Get addresses suitable for connection (not banned, good reputation)
    std::vector<infrastructure::config::authority> get_connectable(size_t max_count) const;

    /// Get addresses to avoid (banned or bad reputation)
    std::vector<infrastructure::config::authority> get_bad_peers() const;

    /// Visit all records (read-only)
    void visit_all(std::function<void(peer_record const&)> visitor) const;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// Load from file
    bool load();

    /// Save to file
    bool save() const;

    /// Import from legacy hosts.cache file
    size_t import_hosts_cache(kth::path const& file_path);

    /// Import from legacy banlist.dat file
    size_t import_banlist(kth::path const& file_path);

private:
    /// Parse a single JSON line from peers.dat using simdjson
    std::optional<peer_record> parse_line(std::string_view line, simdjson::ondemand::parser& parser) const;

    /// Format a record for persistence
    std::string format_record(peer_record const& record) const;

    // Key type for the map (IP address only, port ignored for banning)
    // But we store full authority in the record
    boost::concurrent_flat_map<::asio::ip::address, peer_record, ::kth::salted_ip_hasher> records_;
    kth::path file_path_;
    size_t capacity_;
};

} // namespace kth::network

#endif // KTH_NETWORK_PEER_DATABASE_HPP
