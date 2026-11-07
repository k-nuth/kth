// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/authority.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <exception>
#include <expected>
#include <optional>
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

namespace {

std::string encode_base32(byte_span bytes) {
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz234567";
    std::string result;
    uint32_t bits = 0;
    unsigned available = 0;
    for (auto const byte : bytes) {
        bits = (bits << 8) | byte;
        available += 8;
        while (available >= 5) {
            available -= 5;
            result.push_back(alphabet[(bits >> available) & 31]);
        }
    }
    if (available != 0) result.push_back(alphabet[(bits << (5 - available)) & 31]);
    return result;
}

std::optional<data_chunk> decode_base32(std::string_view text) {
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz234567";
    data_chunk result;
    uint32_t bits = 0;
    unsigned available = 0;
    for (auto const c : text) {
        auto const value = alphabet.find(c);
        if (value == std::string_view::npos) return std::nullopt;
        bits = (bits << 5) | uint32_t(value);
        available += 5;
        if (available >= 8) {
            available -= 8;
            result.push_back(uint8_t(bits >> available));
        }
    }
    // Reject aliases produced by nonzero unused bits in the final character.
    if (available != 0 && (bits & ((1u << available) - 1)) != 0) return std::nullopt;
    return result;
}

std::array<uint8_t, 2> onion_checksum(message::overlay_address const& key) {
    constexpr std::string_view prefix = ".onion checksum";
    std::array<uint64_t, 25> state{};
    data_chunk input(prefix.begin(), prefix.end());
    input.insert(input.end(), key.begin(), key.end());
    input.push_back(3);
    for (size_t i = 0; i < input.size(); ++i) state[i / 8] ^= uint64_t(input[i]) << (8 * (i % 8));
    // SHA3-256 uses a 136-byte rate and the SHA-3 domain separator.
    state[input.size() / 8] ^= uint64_t(0x06) << (8 * (input.size() % 8));
    state[16] ^= uint64_t(0x80) << 56;
    constexpr std::array<uint64_t, 24> constants{
        0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000,
        0x000000000000808b, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
        0x000000000000008a, 0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
        0x000000008000808b, 0x800000000000008b, 0x8000000000008089, 0x8000000000008003,
        0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
        0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008};
    constexpr std::array<int, 25> rotations{
        0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14};
    for (auto const constant : constants) {
        std::array<uint64_t, 5> columns{};
        for (size_t x = 0; x < 5; ++x) {
            for (size_t y = 0; y < 5; ++y) columns[x] ^= state[x + 5 * y];
        }
        std::array<uint64_t, 25> permuted{};
        for (size_t x = 0; x < 5; ++x) {
            auto const delta = columns[(x + 4) % 5] ^ std::rotl(columns[(x + 1) % 5], 1);
            for (size_t y = 0; y < 5; ++y) {
                auto const index = x + 5 * y;
                permuted[y + 5 * ((2 * x + 3 * y) % 5)] = std::rotl(state[index] ^ delta, rotations[index]);
            }
        }
        for (size_t x = 0; x < 5; ++x) {
            for (size_t y = 0; y < 5; ++y) {
                state[x + 5 * y] = permuted[x + 5 * y] ^
                    ((~permuted[(x + 1) % 5 + 5 * y]) & permuted[(x + 2) % 5 + 5 * y]);
            }
        }
        state[0] ^= constant;
    }
    return {uint8_t(state[0]), uint8_t(state[0] >> 8)};
}

} // namespace

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
    auto const colon = text.find(':');
    auto const host = text.substr(0, colon);
    bool const i2p = host.ends_with(".b32.i2p");
    bool const onion = host.ends_with(".onion");
    if (i2p || onion) {
        uint16_t port = 0;
        if (colon != std::string_view::npos) {
            auto const number = text.substr(colon + 1);
            auto const parsed = std::from_chars(number.data(), number.data() + number.size(), port);
            if (number.empty() || parsed.ec != std::errc() || parsed.ptr != number.data() + number.size()) {
                return std::unexpected(error::illegal_value);
            }
        }
        auto const length = i2p ? 52u : 56u;
        auto const suffix = i2p ? 8u : 6u;
        if (host.size() != length + suffix) return std::unexpected(error::illegal_value);
        auto const decoded = decode_base32(host.substr(0, length));
        if ( ! decoded || decoded->size() != (i2p ? 32u : 35u)) return std::unexpected(error::illegal_value);
        message::overlay_address address{};
        std::copy_n(decoded->begin(), address.size(), address.begin());
        if (onion) {
            auto const checksum = onion_checksum(address);
            if ((*decoded)[34] != 3 || (*decoded)[32] != checksum[0] || (*decoded)[33] != checksum[1]) {
                return std::unexpected(error::illegal_value);
            }
        }
        return authority(message::network_address(0, 0,
            i2p ? message::address_network::i2p : message::address_network::torv3, address, port));
    }
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
{
    network_ = address.network();
    overlay_ = address.overlay_address();
}

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
    if (is_i2p()) return encode_base32(overlay_) + ".b32.i2p";
    if (is_tor_v3()) {
        data_chunk bytes(overlay_.begin(), overlay_.end());
        auto const checksum = onion_checksum(overlay_);
        bytes.insert(bytes.end(), checksum.begin(), checksum.end());
        bytes.push_back(3);
        return encode_base32(bytes) + ".onion";
    }
    auto ipv4_hostname = to_ipv4_hostname(ip_);
    return ipv4_hostname.empty() ? to_ipv6_hostname(ip_) : ipv4_hostname;
}

message::network_address authority::to_network_address() const {
    static constexpr uint32_t services = 0;
    static constexpr uint32_t timestamp = 0;
    if (is_overlay()) return message::network_address(timestamp, services, network_, overlay_, port());
    return message::network_address{timestamp, services, ip(), port()};
}

std::string authority::to_string() const {
    return to_authority(to_hostname(), port());
}

} // namespace kth::infrastructure::config
