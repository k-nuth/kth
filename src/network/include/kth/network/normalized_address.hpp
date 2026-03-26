// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_NORMALIZED_ADDRESS_HPP
#define KTH_NETWORK_NORMALIZED_ADDRESS_HPP

#include <asio/ip/address.hpp>

#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

// =============================================================================
// Normalized Address - Type-safe wrapper for normalized IP addresses
// =============================================================================
//
// Guarantees that the IP address is normalized (IPv4-mapped IPv6 converted to IPv4).
// This prevents redundant normalize_ip() calls and makes the code's intent clear.
//
// =============================================================================

struct KN_API normalized_address {
    /// Default constructor (creates invalid 0.0.0.0 address)
    normalized_address() = default;

    /// Construct from asio::ip::address (normalizes automatically)
    explicit
    normalized_address(::asio::ip::address const& addr)
        : addr_(normalize(addr))
    {}

    /// Construct from authority (normalizes automatically)
    explicit 
    normalized_address(infrastructure::config::authority const& auth)
        : addr_(normalize(auth.asio_ip()))
    {}

    /// Access the underlying normalized address
    [[nodiscard]]
    ::asio::ip::address const& get() const noexcept {
        return addr_;
    }

    /// Implicit conversion to asio::ip::address
    operator ::asio::ip::address const&() const noexcept {
        return addr_;
    }

    /// Equality comparison
    bool operator==(normalized_address const&) const noexcept = default;

    /// To string
    [[nodiscard]]
    std::string to_string() const {
        return addr_.to_string();
    }

private:
    /// Normalize: convert IPv4-mapped IPv6 to pure IPv4
    [[nodiscard]]
    static ::asio::ip::address normalize(::asio::ip::address const& addr) {
        if (addr.is_v6()) {
            auto const& v6 = addr.to_v6();
            if (v6.is_v4_mapped()) {
                return ::asio::ip::make_address_v4(::asio::ip::v4_mapped_t{}, v6);
            }
        }
        return addr;
    }

    ::asio::ip::address addr_;
};

} // namespace kth::network

#endif // KTH_NETWORK_NORMALIZED_ADDRESS_HPP
