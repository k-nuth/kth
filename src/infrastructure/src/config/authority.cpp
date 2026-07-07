// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/authority.hpp>

#include <algorithm>
#include <charconv>
#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <ctre.hpp>
#include <fmt/core.h>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::infrastructure::config {

// ---- helpers --------------------------------------------------------------

// host:    [2001:db8::2] or  2001:db8::2  or 1.2.240.1
// returns: [2001:db8::2] or [2001:db8::2] or 1.2.240.1
static
std::string to_host_name(std::string_view host) {
    if (host.find(':') == std::string_view::npos || host.starts_with('[')) {
        return std::string{host};
    }
    return fmt::format("[{}]", host);
}

static
std::string to_authority(std::string_view host, uint16_t port) {
    if (port == 0) {
        return to_host_name(host);
    }
    return fmt::format("{}:{}", to_host_name(host), port);
}

static
std::string to_ipv6(std::string_view ipv4_address) {
    return fmt::format("::ffff:{}", ipv4_address);
}

#if ! defined(__EMSCRIPTEN__)
static
asio::ipv6 to_ipv6(asio::ipv4 const& ipv4_address) {
    auto const ipv6 = to_ipv6(ipv4_address.to_string());
    return ::asio::ip::make_address_v6(ipv6);
}

static
asio::ipv6 to_ipv6(asio::address const& ip_address) {
    if (ip_address.is_v6()) {
        return ip_address.to_v6();
    }
    KTH_ASSERT_MSG(ip_address.is_v4(), "The address must be either IPv4 or IPv6.");
    return to_ipv6(ip_address.to_v4());
}
#endif // ! defined(__EMSCRIPTEN__)

static
std::string to_ipv4_hostname(asio::address const& ip_address) {
#if ! defined(__EMSCRIPTEN__)
    constexpr auto pattern = ctll::fixed_string{"^::ffff:([0-9\\.]+)$"};
    auto const address = ip_address.to_string();
    if (auto m = ctre::match<pattern>(address)) {
        return std::string(m.get<1>());
    }
#endif
    return "";
}

static
std::string to_ipv6_hostname(asio::address const& ip_address) {
#if ! defined(__EMSCRIPTEN__)
    return fmt::format("[{}]", to_ipv6(ip_address));
#else
    return "";
#endif
}

#if ! defined(__EMSCRIPTEN__)
static
asio::ipv6 to_boost_address(message::ip_address const& in) {
    asio::ipv6::bytes_type bytes;
    KTH_ASSERT(bytes.size() == in.size());
    std::copy_n(in.begin(), in.size(), bytes.begin());
    return asio::ipv6{bytes};
}

static
message::ip_address to_bc_address(asio::ipv6 const& in) {
    message::ip_address out;
    auto const bytes = in.to_bytes();
    KTH_ASSERT(bytes.size() == out.size());
    std::copy_n(bytes.begin(), bytes.size(), out.begin());
    return out;
}
#else
static asio::ipv6 to_boost_address(message::ip_address const&) { return 0; }
static message::ip_address to_bc_address(asio::ipv6 const&) { return {}; }
#endif // ! defined(__EMSCRIPTEN__)

// ---- factories -----------------------------------------------------------

// static
authority authority::any() noexcept {
    return authority{message::ip_address{}, 0};
}

// ---- parse_from -----------------------------------------------------------

// static
std::expected<authority, kth::code> authority::parse_from(std::string_view text) {
    constexpr auto pattern = ctll::fixed_string{R"(^(([0-9\.]+)|\[([0-9a-f:\.]+)\])(:([0-9]{1,5}))?$)"};

    auto match = ctre::match<pattern>(text);
    if ( ! match) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const ipv4 = match.get<2>();
    auto const ipv6 = match.get<3>();
    auto const port_group = match.get<5>();

    uint16_t port = 0;
    if (port_group) {
        auto const sv = port_group.to_view();
        auto const result = std::from_chars(sv.data(), sv.data() + sv.size(), port);
        if (result.ec != std::errc()) {
            return std::unexpected(kth::error::illegal_value);
        }
    }

#if defined(__EMSCRIPTEN__)
    // `asio::ipv6` degrades to `int` under Emscripten (see
    // <kth/infrastructure/utility/asio.hpp>), so we can't actually round-trip
    // a textual address through boost::asio. Refuse the parse rather than
    // return an authority holding an uninitialized field.
    return std::unexpected(kth::error::illegal_value);
#else
    std::string const ip_address = ipv6
        ? ipv6.to_string()
        : to_ipv6(ipv4.to_string());

    asio::ipv6 ip;
    try {
        ip = ::asio::ip::make_address_v6(ip_address);
    } catch (std::exception const&) {
        return std::unexpected(kth::error::illegal_value);
    }

    return authority{ip, port};
#endif
}

// static
std::expected<authority, kth::code> authority::parse_from(std::string_view host,
                                                          uint16_t port) {
    return parse_from(to_authority(host, port));
}

// ---- infallible ctors -----------------------------------------------------

authority::authority(message::network_address const& address)
    : authority(address.ip(), address.port())
{}

authority::authority(message::ip_address const& ip, uint16_t port)
    : ip_(to_boost_address(ip)), port_(port)
{}

#if ! defined(__EMSCRIPTEN__)
authority::authority(asio::address const& ip, uint16_t port)
    : ip_(to_ipv6(ip)), port_(port)
{}

authority::authority(asio::endpoint const& endpoint)
    : authority(endpoint.address(), endpoint.port())
{}
#endif // ! defined(__EMSCRIPTEN__)

// ---- accessors ------------------------------------------------------------

asio::ipv6 authority::asio_ip() const {
    return ip_;
}

message::ip_address authority::ip() const {
    return to_bc_address(ip_);
}

uint16_t authority::port() const {
    return port_;
}

std::string authority::to_hostname() const {
    auto ipv4_hostname = to_ipv4_hostname(ip_);
    return ipv4_hostname.empty() ? to_ipv6_hostname(ip_) : ipv4_hostname;
}

message::network_address authority::to_network_address() const {
    static constexpr uint32_t services = 0;
    static constexpr uint32_t timestamp = 0;
    return message::network_address{timestamp, services, ip(), port()};
}

std::string authority::to_string() const {
    return to_authority(to_hostname(), port());
}

} // namespace kth::infrastructure::config
