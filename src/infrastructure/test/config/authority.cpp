// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdint>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;
using namespace kth::infrastructure::config;

// tools.ietf.org/html/rfc4291#section-2.2
#define KI_AUTHORITY_IPV4_ADDRESS                        "1.2.240.1"
#define KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS            "::"
#define KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS             "2001:db8::2"
#define KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS             "::0102:f001"
#define KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "::1.2.240.1"

// tools.ietf.org/html/rfc4291#section-2.5.2
constexpr
message::ip_address test_unspecified_ip_address = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// tools.ietf.org/html/rfc4291#section-2.5.5.2
constexpr
message::ip_address test_mapped_ip_address = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0x01, 0x02, 0xf0, 0x01};

// tools.ietf.org/html/rfc4291#section-2.5.5.1
constexpr
message::ip_address test_compatible_ip_address = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0xf0, 0x01};

constexpr
message::ip_address test_ipv6_address = {
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};

static
bool ip_equal(message::ip_address const& a, message::ip_address const& b) {
    return std::equal(a.begin(), a.end(), b.begin());
}

static
bool net_equal(message::network_address const& a, message::network_address const& b) {
    return ip_equal(a.ip(), b.ip()) && (a.port() == b.port());
}

// ---- port ----------------------------------------------------------------

TEST_CASE("authority::any yields a zero port", "[authority]") {
    REQUIRE(authority::any().port() == 0u);
}

TEST_CASE("authority copy constructor preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    authority const other{test_ipv6_address, expected_port};
    authority const host{other};
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority::parse_from extracts port from an IPv4 host:port string", "[authority]") {
    constexpr uint16_t expected_port = 42;
    std::stringstream address;
    address << KI_AUTHORITY_IPV4_ADDRESS ":" << expected_port;
    auto const host = authority::parse_from(address.str());
    REQUIRE(host.has_value());
    REQUIRE(host->port() == expected_port);
}

TEST_CASE("authority::parse_from extracts port from a bracketed IPv6 host:port string", "[authority]") {
    constexpr uint16_t expected_port = 42;
    std::stringstream address;
    address << "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:" << expected_port;
    auto const host = authority::parse_from(address.str());
    REQUIRE(host.has_value());
    REQUIRE(host->port() == expected_port);
}

TEST_CASE("authority constructor from network_address preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    message::network_address const address{0, 0, test_ipv6_address, expected_port};
    authority const host{address};
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority constructor from ip_address + port preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    authority const host{test_ipv6_address, expected_port};
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority::parse_from(host, port) preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    auto const host = authority::parse_from(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, expected_port);
    REQUIRE(host.has_value());
    REQUIRE(host->port() == expected_port);
}

#if ! defined(__EMSCRIPTEN__)

TEST_CASE("authority constructor from an asio::address preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    auto const address = ::asio::ip::make_address(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    authority const host{address, expected_port};
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority constructor from an asio endpoint preserves the port", "[authority]") {
    constexpr uint16_t expected_port = 42;
    auto const address = ::asio::ip::make_address(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    kth::asio::endpoint const tcp_endpoint{address, expected_port};
    authority const host{tcp_endpoint};
    REQUIRE(host.port() == expected_port);
}

#endif

// ---- ip ------------------------------------------------------------------

TEST_CASE("authority::any exposes the unspecified IP", "[authority]") {
    REQUIRE(ip_equal(authority::any().ip(), test_unspecified_ip_address));
}

TEST_CASE("authority copy constructor preserves the IP", "[authority]") {
    authority const other{test_ipv6_address, 42};
    authority const host{other};
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority::parse_from IPv4 host:port stores an IPv4-mapped IP", "[authority]") {
    auto const host = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS ":42");
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_mapped_ip_address));
}

TEST_CASE("authority::parse_from bracketed IPv6 host:port stores an IPv6 IP", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:42");
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_ipv6_address));
}

TEST_CASE("authority::parse_from IPv6 compatible-notation address matches the alternative form", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_compatible_ip_address));
}

TEST_CASE("authority::parse_from IPv6 alternative-compatible address is stored as compatible", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_compatible_ip_address));
}

TEST_CASE("authority constructor from network_address preserves the IP", "[authority]") {
    message::network_address const address{0, 0, test_ipv6_address, 42};
    authority const host{address};
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority constructor from ip_address + port preserves the IP", "[authority]") {
    authority const host{test_ipv6_address, 42};
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority::parse_from(IPv4 hostname, port) stores an IPv4-mapped IP", "[authority]") {
    auto const host = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS, 42);
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_mapped_ip_address));
}

TEST_CASE("authority::parse_from(IPv6 hostname, port) stores an IPv6 IP", "[authority]") {
    auto const host = authority::parse_from(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, 42);
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_ipv6_address));
}

TEST_CASE("authority::parse_from(bracketed IPv6 hostname, port) stores an IPv6 IP", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]", 42);
    REQUIRE(host.has_value());
    REQUIRE(ip_equal(host->ip(), test_ipv6_address));
}

#if ! defined(__EMSCRIPTEN__)

TEST_CASE("authority constructor from asio::address preserves the IP", "[authority]") {
    auto const address = ::asio::ip::make_address(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    authority const host{address, 42};
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority constructor from an asio endpoint maps IPv4 to IPv4-mapped IPv6", "[authority]") {
    auto const address = ::asio::ip::make_address(KI_AUTHORITY_IPV4_ADDRESS);
    kth::asio::endpoint const tcp_endpoint{address, 42};
    authority const host{tcp_endpoint};
    REQUIRE(ip_equal(host.ip(), test_mapped_ip_address));
}

#endif

// ---- to_hostname ---------------------------------------------------------

TEST_CASE("authority::any renders as the unspecified IPv6 hostname", "[authority]") {
    REQUIRE(authority::any().to_hostname() == "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]");
}

TEST_CASE("authority IPv4-mapped IP renders as bare IPv4", "[authority]") {
    authority const host{test_mapped_ip_address, 0};
    REQUIRE(host.to_hostname() == KI_AUTHORITY_IPV4_ADDRESS);
}

TEST_CASE("authority IPv4-compatible IP renders as the alternative IPv6 form", "[authority]") {
    authority const host{test_compatible_ip_address, 0};
    REQUIRE(host.to_hostname() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
}

TEST_CASE("authority IPv6 IP renders in compressed form", "[authority]") {
    authority const host{test_ipv6_address, 0};
    REQUIRE(host.to_hostname() == "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
}

// ---- to_network_address --------------------------------------------------

TEST_CASE("authority::any round-trips through network_address as unspecified", "[authority]") {
    message::network_address const expected{0, 0, test_unspecified_ip_address, 0};
    REQUIRE(net_equal(authority::any().to_network_address(), expected));
}

TEST_CASE("authority round-trips an IPv4-mapped IP through network_address", "[authority]") {
    message::network_address const expected{0, 0, test_mapped_ip_address, 42};
    authority const host{expected.ip(), expected.port()};
    REQUIRE(net_equal(host.to_network_address(), expected));
}

TEST_CASE("authority round-trips an IPv4-compatible IP through network_address", "[authority]") {
    message::network_address const expected{0, 0, test_compatible_ip_address, 42};
    authority const host{expected.ip(), expected.port()};
    REQUIRE(net_equal(host.to_network_address(), expected));
}

TEST_CASE("authority round-trips an IPv6 IP through network_address", "[authority]") {
    message::network_address const expected{0, 0, test_ipv6_address, 42};
    authority const host{expected.ip(), expected.port()};
    REQUIRE(net_equal(host.to_network_address(), expected));
}

// ---- to_string -----------------------------------------------------------

TEST_CASE("authority::any renders as the unspecified IPv6 address", "[authority]") {
    REQUIRE(authority::any().to_string() == "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]");
}

TEST_CASE("authority::to_string round-trips the unspecified IPv6 address", "[authority]") {
    auto const line = "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string round-trips a bare IPv4 address", "[authority]") {
    auto const line = KI_AUTHORITY_IPV4_ADDRESS;
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string round-trips an IPv4 address with port", "[authority]") {
    auto const line = KI_AUTHORITY_IPV4_ADDRESS ":42";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string round-trips a bare IPv6 address", "[authority]") {
    auto const line = "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string round-trips an IPv6 address with port", "[authority]") {
    auto const line = "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:42";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string normalizes IPv6-compatible addresses to the alternative form", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]");
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
}

TEST_CASE("authority::to_string normalizes IPv6-compatible addresses with port", "[authority]") {
    auto const host = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42");
}

TEST_CASE("authority::to_string round-trips IPv6 alternative-compatible without port", "[authority]") {
    auto const line = "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

TEST_CASE("authority::to_string round-trips IPv6 alternative-compatible with port", "[authority]") {
    auto const line = "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42";
    auto const host = authority::parse_from(line);
    REQUIRE(host.has_value());
    REQUIRE(host->to_string() == line);
}

// ---- equality ------------------------------------------------------------

TEST_CASE("two authority::any values compare equal", "[authority]") {
    REQUIRE(authority::any() == authority::any());
}

TEST_CASE("authority::any is not equal to unspecified IPv6 with a nonzero port", "[authority]") {
    auto const other = authority::parse_from(KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS, 42);
    REQUIRE(other.has_value());
    REQUIRE( ! (authority::any() == *other));
}

TEST_CASE("two authorities parsed from the same IPv4 text compare equal", "[authority]") {
    auto const host1 = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS);
    auto const host2 = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS);
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE(*host1 == *host2);
}

TEST_CASE("bare IPv4 is not equal to the same IPv4 with a port", "[authority]") {
    auto const host1 = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS);
    auto const host2 = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS, 42);
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE( ! (*host1 == *host2));
}

TEST_CASE("IPv4 is not equal to IPv6", "[authority]") {
    auto const host1 = authority::parse_from(KI_AUTHORITY_IPV4_ADDRESS);
    auto const host2 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE( ! (*host1 == *host2));
}

TEST_CASE("two authorities parsed from the same IPv6 text compare equal", "[authority]") {
    auto const host1 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    auto const host2 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE(*host1 == *host2);
}

TEST_CASE("bare IPv6 is not equal to the same IPv6 with a port", "[authority]") {
    auto const host1 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    auto const host2 = authority::parse_from(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, 42);
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE( ! (*host1 == *host2));
}

TEST_CASE("IPv6 compatible and alternative-compatible notations compare equal", "[authority]") {
    auto const host1 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]");
    auto const host2 = authority::parse_from("[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE(*host1 == *host2);
}

// ---- inequality ----------------------------------------------------------

TEST_CASE("two authority::any values are not unequal", "[authority]") {
    REQUIRE( ! (authority::any() != authority::any()));
}

TEST_CASE("authority::any is unequal to unspecified IPv6 with a nonzero port", "[authority]") {
    auto const other = authority::parse_from(KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS, 42);
    REQUIRE(other.has_value());
    REQUIRE(authority::any() != *other);
}

TEST_CASE("two authorities parsed from the same IPv6 text are not unequal", "[authority]") {
    auto const host1 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    auto const host2 = authority::parse_from("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE(host1.has_value());
    REQUIRE(host2.has_value());
    REQUIRE( ! (*host1 != *host2));
}

// ---- parse errors --------------------------------------------------------

TEST_CASE("authority::parse_from rejects a bogus hostname", "[authority]") {
    REQUIRE( ! authority::parse_from("bogus").has_value());
}

TEST_CASE("authority::parse_from rejects an invalid IPv4 literal", "[authority]") {
    REQUIRE( ! authority::parse_from("999.999.999.999").has_value());
}

TEST_CASE("authority::parse_from rejects an invalid IPv6 literal", "[authority]") {
    REQUIRE( ! authority::parse_from("[:::]").has_value());
}

TEST_CASE("authority::parse_from rejects a port that overflows uint16_t", "[authority]") {
    REQUIRE( ! authority::parse_from("[::]:12345678901").has_value());
}
