// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/net_permissions.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>

namespace kth::network {

// =============================================================================
// Permission Utilities
// =============================================================================

std::vector<std::string> to_strings(permission_flags flags) {
    std::vector<std::string> result;

    if (has_permission(flags, permission_flags::bloomfilter)) {
        result.emplace_back("bloomfilter");
    }
    if (has_permission(flags, permission_flags::forcerelay)) {
        result.emplace_back("forcerelay");
    } else if (has_permission(flags, permission_flags::relay)) {
        // Only add relay if forcerelay isn't set (forcerelay implies relay)
        result.emplace_back("relay");
    }
    if (has_permission(flags, permission_flags::noban)) {
        result.emplace_back("noban");
    }
    if (has_permission(flags, permission_flags::mempool)) {
        result.emplace_back("mempool");
    }
    if (has_permission(flags, permission_flags::addr)) {
        result.emplace_back("addr");
    }

    return result;
}

std::expected<permission_flags, std::string> parse_permissions(std::string_view str) {
    auto output = permission_flags::none;

    if (str.empty()) {
        return output;  // Empty string = no permissions
    }

    size_t pos = 0;
    while (pos < str.size()) {
        // Find next comma or end
        auto const comma = str.find(',', pos);
        auto token = str.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        pos = comma == std::string_view::npos ? str.size() : comma + 1;

        // Trim whitespace
        while ( ! token.empty() && std::isspace(int(uint8_t(token.front())))) {
            token.remove_prefix(1);
        }
        while ( ! token.empty() && std::isspace(int(uint8_t(token.back())))) {
            token.remove_suffix(1);
        }

        if (token.empty()) {
            continue;
        }

        // Convert to lowercase for comparison
        std::string lower(token);
        std::ranges::transform(lower, lower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lower == "bloomfilter" || lower == "bloom") {
            add_permission(output, permission_flags::bloomfilter);
        } else if (lower == "forcerelay") {
            add_permission(output, permission_flags::forcerelay);
        } else if (lower == "relay") {
            add_permission(output, permission_flags::relay);
        } else if (lower == "noban") {
            add_permission(output, permission_flags::noban);
        } else if (lower == "mempool") {
            add_permission(output, permission_flags::mempool);
        } else if (lower == "addr") {
            add_permission(output, permission_flags::addr);
        } else if (lower == "all") {
            add_permission(output, permission_flags::all);
        } else {
            return std::unexpected("Unknown permission: " + lower);
        }
    }

    return output;
}

// =============================================================================
// Whitelist Permission Entry
// =============================================================================

std::expected<whitelist_permissions, std::string> parse_whitelist(std::string_view str) {
    if (str.empty()) {
        return std::unexpected("Empty whitelist entry");
    }

    whitelist_permissions output;

    // Check for permissions@address format
    auto const at_pos = str.find('@');
    std::string_view address_str;
    std::string_view permissions_str;

    if (at_pos != std::string_view::npos) {
        permissions_str = str.substr(0, at_pos);
        address_str = str.substr(at_pos + 1);
    } else {
        // No @ sign - entire string is the address, use implicit permissions
        address_str = str;
        output.flags = permission_flags::is_implicit;
    }

    // Parse permissions if present
    if ( ! permissions_str.empty()) {
        auto result = parse_permissions(permissions_str);
        if ( ! result) {
            return std::unexpected(result.error());
        }
        output.flags = *result;
    }

    // Parse address/subnet
    if (address_str.empty()) {
        return std::unexpected("Missing address in whitelist entry");
    }

    // Pull off an optional CIDR prefix (e.g. "10.0.0.0/8" or "::/0").
    // Default 128 bits = exact match against the 16-byte IPv6 representation.
    output.prefix_bits = 128;
    auto const slash_pos = address_str.rfind('/');
    if (slash_pos != std::string_view::npos) {
        auto const prefix_str = address_str.substr(slash_pos + 1);
        unsigned int prefix = 0;
        auto const [end_ptr, ec] = std::from_chars(
            prefix_str.data(), prefix_str.data() + prefix_str.size(), prefix);
        if (ec != std::errc{} || end_ptr != prefix_str.data() + prefix_str.size()) {
            return std::unexpected("Invalid subnet prefix: " + std::string(prefix_str));
        }
        // IPv4 inputs (contain '.') count over the v4 octets — store the
        // equivalent IPv6 bit count (96 + N). IPv6 inputs (contain ':') are
        // already in IPv6 bits.
        bool const is_v4 = address_str.substr(0, slash_pos).find('.') != std::string_view::npos;
        unsigned int const max_bits = is_v4 ? 32u : 128u;
        if (prefix > max_bits) {
            return std::unexpected("Subnet prefix out of range: /" + std::string(prefix_str));
        }
        output.prefix_bits = static_cast<uint8_t>(is_v4 ? 96u + prefix : prefix);
        address_str = address_str.substr(0, slash_pos);
    }

    // Try to parse as authority (IP:port or just IP)
    auto parsed = infrastructure::config::authority::parse_from(address_str);
    if ( ! parsed) {
        return std::unexpected("Invalid address: " + std::string(address_str));
    }
    output.subnet = std::move(*parsed);

    return output;
}

// =============================================================================
// Whitebind Permission Entry
// =============================================================================

std::expected<whitebind_permissions, std::string> parse_whitebind(std::string_view str) {
    if (str.empty()) {
        return std::unexpected("Empty whitebind entry");
    }

    whitebind_permissions output;

    // Check for permissions@address format
    auto const at_pos = str.find('@');
    std::string_view address_str;
    std::string_view permissions_str;

    if (at_pos != std::string_view::npos) {
        permissions_str = str.substr(0, at_pos);
        address_str = str.substr(at_pos + 1);
    } else {
        // No @ sign - entire string is the bind address, use implicit permissions
        address_str = str;
        output.flags = permission_flags::is_implicit;
    }

    // Parse permissions if present
    if ( ! permissions_str.empty()) {
        auto result = parse_permissions(permissions_str);
        if ( ! result) {
            return std::unexpected(result.error());
        }
        output.flags = *result;
    }

    // Parse bind address
    if (address_str.empty()) {
        return std::unexpected("Missing bind address in whitebind entry");
    }

    auto parsed = infrastructure::config::authority::parse_from(address_str);
    if ( ! parsed) {
        return std::unexpected("Invalid bind address: " + std::string(address_str));
    }
    output.bind_address = std::move(*parsed);

    return output;
}

} // namespace kth::network
