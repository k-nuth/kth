// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_BANLIST_HPP
#define KTH_NETWORK_BANLIST_HPP

#include <chrono>
#include <expected>
#include <string>

#include <boost/unordered/concurrent_flat_map.hpp>

#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

// =============================================================================
// Ban Reason (BCHN-style)
// =============================================================================

enum class ban_reason : uint8_t {
    unknown = 0,
    node_misbehaving = 1,      // Misbehavior (invalid messages, etc.)
    manually_added = 2,        // Manually banned by user
    checkpoint_failed = 3,     // Sent headers failing checkpoint (wrong chain)
};

// =============================================================================
// Ban Entry
// =============================================================================
// Represents a single ban entry with creation time, expiration, and reason.
// Based on BCHN's CBanEntry.

struct KN_API ban_entry {
    using clock = std::chrono::system_clock;
    using time_point = clock::time_point;

    time_point create_time{clock::now()};
    time_point ban_until{};
    ban_reason reason{ban_reason::unknown};

    /// Check if the ban has expired
    [[nodiscard]]
    constexpr bool is_expired() const {
        return clock::now() >= ban_until;
    }

    /// Get remaining ban duration
    [[nodiscard]]
    std::chrono::seconds remaining() const {
        auto const now = clock::now();
        if (now >= ban_until) {
            return std::chrono::seconds{0};
        }
        return std::chrono::duration_cast<std::chrono::seconds>(ban_until - now);
    }
};

// =============================================================================
// Banlist
// =============================================================================
// Thread-safe banlist using boost::concurrent_flat_map.
// Based on BCHN's CBanDB.
// Bans by IP address only (port is ignored).

class KN_API banlist {
public:
    using clock = std::chrono::system_clock;
    static constexpr auto default_ban_duration = std::chrono::hours{24};

    /// Construct banlist with optional persistence file
    explicit banlist(kth::path file_path = {});

    /// Ban an IP address
    /// @param ip The IP address to ban
    /// @param duration How long to ban (default 24 hours)
    /// @param reason Why the peer is being banned
    void ban(
        ::asio::ip::address const& ip,
        std::chrono::seconds duration = default_ban_duration,
        ban_reason reason = ban_reason::node_misbehaving);

    /// Ban an authority (IP:port, port is ignored)
    void ban(
        infrastructure::config::authority const& authority,
        std::chrono::seconds duration = default_ban_duration,
        ban_reason reason = ban_reason::node_misbehaving);

    /// Unban an IP address
    /// @return true if the IP was banned and is now unbanned
    bool unban(::asio::ip::address const& ip);

    /// Unban an authority
    bool unban(infrastructure::config::authority const& authority);

    /// Check if an IP address is banned
    [[nodiscard]]
    bool is_banned(::asio::ip::address const& ip) const;

    /// Check if an authority is banned
    [[nodiscard]]
    bool is_banned(infrastructure::config::authority const& authority) const;

    /// Get ban entry for an IP (if banned)
    [[nodiscard]]
    std::optional<ban_entry> get_ban(::asio::ip::address const& ip) const;

    /// Get number of bans (includes expired, use sweep_expired() to clean)
    [[nodiscard]]
    size_t size() const;

    /// Clear all bans
    void clear();

    /// Remove expired bans
    void sweep_expired();

    /// Load banlist from file
    /// @return true on success, false on failure
    bool load();

    /// Save banlist to file
    /// @return true on success, false on failure
    bool save() const;

    /// Get all current bans (for debugging/display)
    [[nodiscard]]
    std::vector<std::pair<std::string, ban_entry>> get_all_bans() const;

private:
    // boost::concurrent_flat_map provides thread-safe operations without mutex
    // Uses salted_ip_hasher for hash table collision resistance (BCHN-style)
    boost::concurrent_flat_map<::asio::ip::address, ban_entry, ::kth::salted_ip_hasher> bans_;
    kth::path file_path_;
};

// =============================================================================
// Helper functions
// =============================================================================

/// Convert ban_reason to string
[[nodiscard]]
constexpr std::string_view to_string(ban_reason reason) {
    switch (reason) {
        case ban_reason::unknown: return "unknown";
        case ban_reason::node_misbehaving: return "misbehaving";
        case ban_reason::manually_added: return "manually added";
        case ban_reason::checkpoint_failed: return "checkpoint failed (wrong chain)";
    }
    return "unknown";
}

} // namespace kth::network

#endif // KTH_NETWORK_BANLIST_HPP
