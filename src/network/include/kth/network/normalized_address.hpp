// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_NORMALIZED_ADDRESS_HPP
#define KTH_NETWORK_NORMALIZED_ADDRESS_HPP

#include <asio/ip/address.hpp>

#include <array>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/salted_hashers.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

// =============================================================================
// Normalized Address - Type-safe wrapper for normalized IP addresses
// =============================================================================
//
// IP addresses are normalized (IPv4-mapped IPv6 converted to IPv4). Overlay
// addresses keep their network suffix so they cannot share an IP's record.
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
        : addr_(auth.is_overlay() ? ::asio::ip::address{} : normalize(auth.asio_ip()))
        , overlay_(auth.is_overlay() ? auth.to_hostname() : std::string{})
    {}

    [[nodiscard]]
    bool is_overlay() const noexcept {
        return ! overlay_.empty();
    }

    [[nodiscard]]
    std::string const& overlay() const noexcept {
        return overlay_;
    }

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
        return is_overlay() ? overlay_ : addr_.to_string();
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
    std::string overlay_;
};

struct KN_API salted_address_hasher {
    salted_address_hasher() noexcept {
        pseudo_random::fill(salt_);
    }

    [[nodiscard]]
    size_t operator()(normalized_address const& address) const noexcept {
        if ( ! address.is_overlay()) {
            return ip_hasher_(address.get());
        }
        auto const& host = address.overlay();
        return size_t(sip_hasher(salt_[0], salt_[1])
            .write(reinterpret_cast<uint8_t const*>(host.data()), host.size()).finalize());
    }

private:
    kth::salted_ip_hasher ip_hasher_;
    std::array<uint64_t, 2> salt_{};
};

} // namespace kth::network

#endif // KTH_NETWORK_NORMALIZED_ADDRESS_HPP
