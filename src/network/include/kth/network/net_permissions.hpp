// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_NET_PERMISSIONS_HPP
#define KTH_NETWORK_NET_PERMISSIONS_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

// =============================================================================
// Permission Flags
// =============================================================================
// Based on BCHN's NetPermissionFlags (net_permissions.h)
//
// These flags control what a peer is allowed to do and whether they can be
// banned/disconnected for misbehavior.

enum class permission_flags : uint32_t {
    none = 0,

    // Can query bloomfilter even if bloom filters are disabled
    bloomfilter = 1U << 1,

    // Always relay transactions from this peer, even if already in mempool
    // or rejected from policy. Implies relay.
    forcerelay = 1U << 2,

    // Relay and accept transactions from this peer, even if -blocksonly is true.
    // This peer is also not subject to limits on how many transaction INVs are tracked.
    relay = 1U << 3,

    // Can't be banned/disconnected/discouraged for misbehavior.
    // Used for trusted peers (whitelisted IPs, manual connections).
    noban = 1U << 4,

    // Can query the mempool
    mempool = 1U << 5,

    // Can request addrs without hitting a privacy-preserving cache,
    // and send us unlimited amounts of addrs.
    addr = 1U << 7,

    // True if the user did not specifically set fine-grained permissions.
    // When set, default permissions are applied based on legacy whitelist behavior.
    is_implicit = 1U << 31,

    // All permissions combined
    all = bloomfilter | forcerelay | relay | noban | mempool | addr
};

// Bitwise operators for permission_flags
constexpr permission_flags operator|(permission_flags a, permission_flags b) {
    return permission_flags(uint32_t(a) | uint32_t(b));
}

constexpr permission_flags operator&(permission_flags a, permission_flags b) {
    return permission_flags(uint32_t(a) & uint32_t(b));
}

constexpr permission_flags operator~(permission_flags a) {
    return permission_flags(~uint32_t(a));
}

constexpr permission_flags& operator|=(permission_flags& a, permission_flags b) {
    return a = a | b;
}

constexpr permission_flags& operator&=(permission_flags& a, permission_flags b) {
    return a = a & b;
}

// =============================================================================
// Permission Utilities (free functions)
// =============================================================================

/// Check if a specific permission flag is set
[[nodiscard]]
constexpr bool has_permission(permission_flags flags, permission_flags flag) {
    return (flags & flag) == flag;
}

/// Add a permission flag
constexpr void add_permission(permission_flags& flags, permission_flags flag) {
    flags |= flag;
}

/// Remove a permission flag
constexpr void clear_permission(permission_flags& flags, permission_flags flag) {
    flags &= ~flag;
}

/// Convert permission flags to human-readable strings
[[nodiscard]]
KN_API std::vector<std::string> to_strings(permission_flags flags);

/// Parse permission string (e.g., "noban,relay,mempool")
/// Returns permission_flags on success, error message on failure
[[nodiscard]]
KN_API std::expected<permission_flags, std::string> parse_permissions(std::string_view str);

// =============================================================================
// Whitelist Permission Entry
// =============================================================================
// Represents a whitelisted subnet with associated permissions.
// Parsed from config like: -whitelist=noban,relay@192.168.1.0/24

struct KN_API whitelist_permissions {
    permission_flags flags{permission_flags::none};
    infrastructure::config::authority subnet;  // IP or subnet to match
};

/// Parse whitelist entry string
/// Format: [permissions@]<IP|subnet>
/// Examples:
///   "192.168.1.0/24"           -> implicit permissions
///   "noban@192.168.1.0/24"     -> noban permission
///   "noban,relay@10.0.0.1"     -> noban + relay permissions
[[nodiscard]]
KN_API std::expected<whitelist_permissions, std::string> parse_whitelist(std::string_view str);

// =============================================================================
// Whitebind Permission Entry
// =============================================================================
// Represents a bind address with associated permissions for incoming connections.
// Parsed from config like: -whitebind=noban@0.0.0.0:8333

struct KN_API whitebind_permissions {
    permission_flags flags{permission_flags::none};
    infrastructure::config::authority bind_address;
};

/// Parse whitebind entry string
/// Format: [permissions@]<bind_address>
[[nodiscard]]
KN_API std::expected<whitebind_permissions, std::string> parse_whitebind(std::string_view str);

} // namespace kth::network

#endif // KTH_NETWORK_NET_PERMISSIONS_HPP
