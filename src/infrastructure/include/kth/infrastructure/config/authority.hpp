// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONFIG_AUTHORITY_HPP
#define KTH_INFRASTRUCTURE_CONFIG_AUTHORITY_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/asio.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a network authority: `{ip address, port}`.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * inputs (host-port strings) go through `parse_from` returning
 * `expect<authority>`.
 */
struct KI_API authority {
    using list = std::vector<authority>;

    /**
     * Sentinel "unspecified" authority (`[::]:0`). Kept as a factory so
     * callers spell out that they want the empty-address form; the type
     * itself remains valid-by-construction.
     */
    [[nodiscard]] static
    authority any() noexcept;

    /**
     * Parse a text authority (`host:port`, IPv4 or bracketed IPv6).
     * Returns `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    std::expected<authority, kth::code> parse_from(std::string_view text);

    /**
     * Non-throwing convenience: parses `host` (which may include
     * brackets around an IPv6) and pairs it with the given port.
     */
    [[nodiscard]] static
    std::expected<authority, kth::code> parse_from(std::string_view host, uint16_t port);

    /**
     * Wrap a wire-protocol network_address.
     * (Kept implicit to preserve existing kth-network call sites.)
     */
    // implicit
    authority(message::network_address const& address);

    authority(message::ip_address const& ip, uint16_t port);

#if ! defined(__EMSCRIPTEN__)
    authority(asio::address const& ip, uint16_t port);

    explicit
    authority(asio::endpoint const& endpoint);
#endif

    [[nodiscard]] asio::ipv6            asio_ip() const;
    [[nodiscard]] message::ip_address   ip() const;
    [[nodiscard]] uint16_t              port() const;
    [[nodiscard]] std::string           to_hostname() const;
    [[nodiscard]] std::string           to_string() const;
    [[nodiscard]] message::network_address to_network_address() const;

    [[nodiscard]] bool operator==(authority const& x) const;
    [[nodiscard]] bool operator!=(authority const& x) const;

private:
    // Private "wrap only" ctor used by `parse_from`.
    authority(asio::ipv6 const& ip, uint16_t port)
        : ip_(ip), port_(port) {}

    asio::ipv6 ip_;
    uint16_t   port_{0};
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::authority>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::authority const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
