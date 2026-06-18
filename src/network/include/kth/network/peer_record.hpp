// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_RECORD_HPP
#define KTH_NETWORK_PEER_RECORD_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/banlist.hpp>  // for ban_reason
#include <kth/network/normalized_address.hpp>

namespace kth::network {

// =============================================================================
// Ban Info
// =============================================================================

struct ban_info {
    std::chrono::system_clock::time_point banned_until;
    ban_reason reason{ban_reason::unknown};

    [[nodiscard]]
    bool is_expired() const {
        return std::chrono::system_clock::now() >= banned_until;
    }
};

// =============================================================================
// Performance Stats
// =============================================================================

struct peer_performance {
    uint64_t total_blocks{0};
    uint64_t total_time_ms{0};
    uint32_t sample_count{0};

    [[nodiscard]]
    double avg_ms_per_block() const {
        return total_blocks > 0 ? double(total_time_ms) / double(total_blocks) : 0.0;
    }

    void record(uint32_t blocks, uint32_t time_ms) {
        total_blocks += blocks;
        total_time_ms += time_ms;
        ++sample_count;
    }

    void reset() {
        total_blocks = 0;
        total_time_ms = 0;
        sample_count = 0;
    }
};

// =============================================================================
// Peer Record - Unified peer information
// =============================================================================
//
// Stores all known information about a peer, whether currently connected or not.
// This replaces the separate banlist.dat and hosts.cache files with a single
// unified database.
//
// =============================================================================

struct KN_API peer_record {
    using clock = std::chrono::system_clock;
    using time_point = clock::time_point;

    // -------------------------------------------------------------------------
    // Identity
    // -------------------------------------------------------------------------
    infrastructure::config::authority authority;
    normalized_address ip;                      // Cached normalized IP (avoids repeated normalization)
    std::string user_agent;                     // e.g., "BCHN:28.0.1"
    uint64_t services{0};                       // NODE_NETWORK, NODE_BLOOM, etc.

    // -------------------------------------------------------------------------
    // Timestamps
    // -------------------------------------------------------------------------
    time_point first_seen{};      // When we first added this peer to our database
    time_point last_seen{};       // When we last received info about this peer (our clock)
    time_point last_active{};     // Timestamp from addr message (when network says peer was active)
    time_point last_attempt{};    // When we last tried to connect
    time_point last_success{};    // When we last successfully connected

    // -------------------------------------------------------------------------
    // Reputation
    // -------------------------------------------------------------------------
    // Score accumulates with misbehavior: 0 = good, 100+ = ban threshold
    // Decays over time with good behavior
    int reputation_score{0};

    // -------------------------------------------------------------------------
    // Ban Info (optional - null if not banned)
    // -------------------------------------------------------------------------
    std::optional<ban_info> ban;

    // -------------------------------------------------------------------------
    // Performance Stats
    // -------------------------------------------------------------------------
    peer_performance performance;

    // -------------------------------------------------------------------------
    // Connection Stats
    // -------------------------------------------------------------------------
    uint32_t connection_attempts{0};
    uint32_t connection_successes{0};
    uint32_t connection_failures{0};

    // -------------------------------------------------------------------------
    // Helper Methods
    // -------------------------------------------------------------------------

    /// Check if this peer is currently banned (and ban hasn't expired)
    [[nodiscard]]
    bool is_banned() const {
        return ban.has_value() && !ban->is_expired();
    }

    /// Check if this peer should be avoided for sync (bad reputation but not banned)
    [[nodiscard]]
    bool is_bad_for_sync() const {
        return reputation_score >= 50;  // Half of ban threshold
    }

    /// Get success rate (0.0 to 1.0)
    [[nodiscard]]
    double success_rate() const {
        if (connection_attempts == 0) return 0.0;
        return double(connection_successes) / double(connection_attempts);
    }

    /// Record a connection attempt
    void record_attempt() {
        last_attempt = clock::now();
        ++connection_attempts;
    }

    /// Record a successful connection
    void record_success() {
        last_success = clock::now();
        last_seen = last_success;
        ++connection_successes;
    }

    /// Record a failed connection
    void record_failure() {
        ++connection_failures;
    }

    /// Add misbehavior score, returns true if now exceeds ban threshold
    bool add_misbehavior(int score, int ban_threshold = 100) {
        reputation_score += score;
        return reputation_score >= ban_threshold;
    }

    /// Decay reputation score (call periodically for good behavior)
    void decay_reputation(int amount = 1) {
        reputation_score = std::max(0, reputation_score - amount);
    }

    /// Ban this peer
    void set_banned(std::chrono::seconds duration, ban_reason reason) {
        ban = ban_info{
            .banned_until = clock::now() + duration,
            .reason = reason
        };
    }

    /// Unban this peer
    void clear_ban() {
        ban.reset();
    }
};

// ban_reason serialization helpers are in banlist.hpp (to_string, ban_reason_from_string)

} // namespace kth::network

#endif // KTH_NETWORK_PEER_RECORD_HPP
