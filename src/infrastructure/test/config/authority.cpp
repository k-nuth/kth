// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sstream>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

// #include <test_helpers.hpp>

#include <catch2/catch_test_macros.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;
using namespace kth::infrastructure::config;
#if ! defined(__EMSCRIPTEN__)
using namespace boost::program_options;
#endif

// Start Test Suite: authority tests

// tools.ietf.org/html/rfc4291#section-2.2
#define KI_AUTHORITY_IPV4_ADDRESS "1.2.240.1"
#define KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "::"
#define KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "2001:db8::2"
#define KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "::0102:f001"
#define KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "::1.2.240.1"
#define KI_AUTHORITY_IPV4_BOGUS_ADDRESS "0.0.0.57:256"
#define KI_AUTHORITY_IPV6_BOGUS_IPV4_ADDRESS "[::ffff:0:39]:256"

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
constexpr message::ip_address test_compatible_ip_address = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0xf0, 0x01
};

constexpr message::ip_address test_ipv6_address = {
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

static bool ip_equal(message::ip_address const& a, message::ip_address const& b) {
    return std::equal(a.begin(), a.end(), b.begin());
}

static bool net_equal(message::network_address const& a, message::network_address const& b) {
    return ip_equal(a.ip(), b.ip()) && (a.port() == b.port());
}

// ------------------------------------------------------------------------- //

// Start Test Suite: authority port

TEST_CASE("authority port default zero", "[authority port]") {
    authority const host {};
    REQUIRE(host.port() == 0u);
}

TEST_CASE("authority port copy expected", "[authority port]") {
    const uint16_t expected_port = 42;
    const authority other(test_ipv6_address, expected_port);
    const authority host(other);
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority should extract port from IPv4 address with port", "[authority port]") {
    const uint16_t expected_port = 42;
    std::stringstream address;
    address << KI_AUTHORITY_IPV4_ADDRESS ":" << expected_port;
    const authority host(address.str());
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority should extract port from IPv6 address with port", "[authority port]") {
    const uint16_t expected_port = 42;
    std::stringstream address;
    address << "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:" << expected_port;
    const authority host(address.str());
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority should extract port from network address", "[authority port]") {
    const uint16_t expected_port = 42;
    message::network_address const address {
        0, 0, test_ipv6_address, expected_port
    };

    const authority host(address);
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority should extract port from IP address constructor", "[authority port]") {
    const uint16_t expected_port = 42;
    const authority host(test_ipv6_address, expected_port);
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority should extract port from hostname constructor", "[authority port]") {
    const uint16_t expected_port = 42;
    const authority host(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, expected_port);
    REQUIRE(host.port() == expected_port);
}

#if ! defined(__EMSCRIPTEN__)
TEST_CASE("authority  port  boost address  expected", "[authority  port]") {
    const uint16_t expected_port = 42;
    auto const address = kth::asio::address::from_string(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    const authority host(address, expected_port);
    REQUIRE(host.port() == expected_port);
}

TEST_CASE("authority  port  boost endpoint  expected", "[authority  port]") {
    const uint16_t expected_port = 42;
    auto const address = kth::asio::address::from_string(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    kth::asio::endpoint tcp_endpoint(address, expected_port);
    const authority host(tcp_endpoint);
    REQUIRE(host.port() == expected_port);
}
#endif

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  ip

TEST_CASE("authority  bool  default  false", "[authority  ip]") {
    authority const host {};
    REQUIRE( ! host);
}

TEST_CASE("authority  bool  zero port  false", "[authority  ip]") {
    const authority host(test_ipv6_address, 0);
    REQUIRE( ! host);
}

TEST_CASE("authority  bool  nonzero port  true", "[authority  ip]") {
    const authority host(test_ipv6_address, 42);
    REQUIRE(host);
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  ip

TEST_CASE("authority  ip  default  unspecified", "[authority  ip]") {
    authority const host {};
    REQUIRE(ip_equal(host.ip(), test_unspecified_ip_address));
}

TEST_CASE("authority  ip  copy  expected", "[authority  ip]") {
    auto const& expected_ip = test_ipv6_address;
    const authority other(expected_ip, 42);
    const authority host(other);
    REQUIRE(ip_equal(host.ip(), expected_ip));
}

TEST_CASE("authority should parse IPv4 address from string", "[authority ip]") {
    const authority host(KI_AUTHORITY_IPV4_ADDRESS ":42");
    REQUIRE(ip_equal(host.ip(), test_mapped_ip_address));
}

TEST_CASE("authority should parse IPv6 address from string", "[authority ip]") {
    const authority host("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:42");
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority should parse IPv6 compatible address from string", "[authority ip]") {
    // KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS A|B variants are equivalent.
    const authority host("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(ip_equal(host.ip(), test_compatible_ip_address));
}

TEST_CASE("authority should parse IPv6 alternative compatible address from string", "[authority ip]") {
    // KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS A|B variants are equivalent.
    const authority host("[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(ip_equal(host.ip(), test_compatible_ip_address));
}

TEST_CASE("authority should extract IP from network address", "[authority ip]") {
    auto const& expected_ip = test_ipv6_address;
    message::network_address const address {
        0, 0, test_ipv6_address, 42
    };

    const authority host(address);
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority should extract IP from IP address constructor", "[authority ip]") {
    auto const& expected_ip = test_ipv6_address;
    const authority host(expected_ip, 42);
    REQUIRE(ip_equal(host.ip(), expected_ip));
}

TEST_CASE("authority should parse IPv4 from hostname string", "[authority ip]") {
    const authority host(KI_AUTHORITY_IPV4_ADDRESS, 42);
    REQUIRE(ip_equal(host.ip(), test_mapped_ip_address));
}

TEST_CASE("authority should parse IPv6 from hostname string", "[authority ip]") {
    const authority host(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, 42);
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority should parse IPv6 from bracketed hostname string", "[authority ip]") {
    const authority host("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]", 42);
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

#if ! defined(__EMSCRIPTEN__)
TEST_CASE("authority  ip  boost address  expected", "[authority  ip]") {
    auto const address = kth::asio::address::from_string(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS);
    const authority host(address, 42);
    REQUIRE(ip_equal(host.ip(), test_ipv6_address));
}

TEST_CASE("authority  ip  boost endpoint  expected", "[authority  ip]") {
    auto const address = kth::asio::address::from_string(KI_AUTHORITY_IPV4_ADDRESS);
    kth::asio::endpoint tcp_endpoint(address, 42);
    const authority host(tcp_endpoint);
    REQUIRE(ip_equal(host.ip(), test_mapped_ip_address));
}
#endif

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  to hostname

TEST_CASE("authority  to hostname  default  ipv6 unspecified", "[authority  to hostname]") {
    authority const host {};
    REQUIRE(host.to_hostname() == "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]");
}

TEST_CASE("authority  to hostname  ipv4 mapped ip address  ipv4", "[authority  to hostname]") {
    // A mapped ip address serializes as IPv4.
    const authority host(test_mapped_ip_address, 0);
    REQUIRE(host.to_hostname() == KI_AUTHORITY_IPV4_ADDRESS);
}

TEST_CASE("authority  to hostname  ipv4 compatible ip address  ipv6 alternative", "[authority  to hostname]") {
    // A compatible ip address serializes as alternative notation IPv6.
    const authority host(test_compatible_ip_address, 0);
    REQUIRE(host.to_hostname() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
}

TEST_CASE("authority  to hostname  ipv6 address  ipv6 compressed", "[authority  to hostname]") {
    // An ipv6 address serializes using compression.
    const authority host(test_ipv6_address, 0);
    REQUIRE(host.to_hostname() == "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  to network address

TEST_CASE("authority  to network address  default  ipv6 unspecified", "[authority  to network address]") {
    message::network_address const expected_address {
        0, 0, test_unspecified_ip_address, 0,
    };

    authority const host {};
    REQUIRE(net_equal(host.to_network_address(), expected_address));
}

TEST_CASE("authority  to network address  ipv4 mapped ip address  ipv4", "[authority  to network address]") {
    message::network_address const expected_address {
        0, 0, test_mapped_ip_address, 42,
    };

    const authority host(expected_address.ip(), expected_address.port());
    REQUIRE(net_equal(host.to_network_address(), expected_address));
}

TEST_CASE("authority  to network address  ipv4 compatible ip address  ipv6 alternative", "[authority  to network address]") {
    message::network_address const expected_address {
        0, 0, test_compatible_ip_address, 42,
    };

    const authority host(expected_address.ip(), expected_address.port());
    REQUIRE(net_equal(host.to_network_address(), expected_address));
}

TEST_CASE("authority  to network address  ipv6 address  ipv6 compressed", "[authority  to network address]") {
    message::network_address const expected_address {
        0, 0, test_ipv6_address, 42,
    };

    const authority host(expected_address.ip(), expected_address.port());
    REQUIRE(net_equal(host.to_network_address(), expected_address));
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  to string

TEST_CASE("authority  to string  default  unspecified", "[authority  to string]") {
    authority const host {};
    REQUIRE(host.to_string() == "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]");
}

TEST_CASE("authority  to string  unspecified  unspecified", "[authority  to string]") {
    auto const line = "[" KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS "]";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

// These results vary by Boost version, so these tests are disabled.
////TEST_CASE("authority  to string  bogus ipv4  ipv4", "[authority  to string]")
////{
////    authority host(KI_AUTHORITY_IPV4_BOGUS_ADDRESS);
////    REQUIRE(host.to_string() == KI_AUTHORITY_IPV4_BOGUS_ADDRESS);
////}
////TEST_CASE("authority  to string  bogus ipv4  ipv6 compatible", "[authority  to string]")
////{
////    authority host(KI_AUTHORITY_IPV4_BOGUS_ADDRESS);
////    REQUIRE(host.to_string() == KI_AUTHORITY_IPV6_BOGUS_IPV4_ADDRESS);
////}

TEST_CASE("authority  to string  ipv4  expected", "[authority  to string]") {
    auto const line = KI_AUTHORITY_IPV4_ADDRESS;
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

TEST_CASE("authority  to string  ipv4 port  expected", "[authority  to string]") {
    auto const line = KI_AUTHORITY_IPV4_ADDRESS ":42";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

TEST_CASE("authority  to string  ipv6  expected", "[authority  to string]") {
    auto const line = "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

TEST_CASE("authority  to string  ipv6 port  expected", "[authority  to string]") {
    auto const line = "[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]:42";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

TEST_CASE("authority  to string  ipv6 compatible  expected", "[authority  to string]") {
    // A compatible ip address serializes as alternative notation IPv6.
    const authority host("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]");
    REQUIRE(host.to_string() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
}

TEST_CASE("authority  to string  ipv6 alternative compatible port  expected", "[authority  to string]") {
    // A compatible ip address serializes as alternative notation IPv6.
    const authority host("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]:42");
    REQUIRE(host.to_string() == "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42");
}

TEST_CASE("authority  to string  ipv6 alternative compatible  expected", "[authority  to string]") {
    auto const line = "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

TEST_CASE("authority  to string  ipv6 compatible port  expected", "[authority  to string]") {
    auto const line = "[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]:42";
    const authority host(line);
    REQUIRE(host.to_string() == line);
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  equality

TEST_CASE("authority  equality  default default  true", "[authority  equality]") {
    authority const host1 {};
    authority const host2 {};
    REQUIRE(host1 == host2);
}

TEST_CASE("authority  equality  default unspecified port  false", "[authority  equality]") {
    authority const host1 {};
    const authority host2(KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS, 42);
    REQUIRE( ! (host1 == host2));
}

TEST_CASE("authority  equality  ipv4 ipv4  true", "[authority  equality]") {
    const authority host1(KI_AUTHORITY_IPV4_ADDRESS);
    const authority host2(KI_AUTHORITY_IPV4_ADDRESS);
    REQUIRE(host1 == host2);
}

TEST_CASE("authority  equality  ipv4 ipv4 port  true", "[authority  equality]") {
    const authority host1(KI_AUTHORITY_IPV4_ADDRESS);
    const authority host2(KI_AUTHORITY_IPV4_ADDRESS, 42);
    REQUIRE( ! (host1 == host2));
}

TEST_CASE("authority  equality  ipv4 ipv6  false", "[authority  equality]") {
    const authority host1(KI_AUTHORITY_IPV4_ADDRESS);
    const authority host2("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE( ! (host1 == host2));
}

TEST_CASE("authority  equality  ipv6 ipv6  true", "[authority  equality]") {
    const authority host1("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    const authority host2("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE(host1 == host2);
}

TEST_CASE("authority  equality  ipv6 ipv6 port  false", "[authority  equality]") {
    const authority host1("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    const authority host2(KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS, 42);
    REQUIRE( ! (host1 == host2));
}

TEST_CASE("authority  equality  compatible alternative  true", "[authority  equality]") {
    // A compatible ip address is equivalent to its alternative addressing.
    const authority host1("[" KI_AUTHORITY_IPV6_COMPATIBLE_ADDRESS "]");
    const authority host2("[" KI_AUTHORITY_IPV6_ALTERNATIVE_COMPATIBLE_ADDRESS "]");
    REQUIRE(host1 == host2);
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  inequality

TEST_CASE("authority  inequality  default default  false", "[authority  inequality]") {
    authority const host1 {};
    authority const host2 {};
    REQUIRE( ! (host1 != host2));
}

TEST_CASE("authority  inequality  default unspecified port  true", "[authority  inequality]") {
    authority const host1 {};
    const authority host2(KI_AUTHORITY_IPV6_UNSPECIFIED_ADDRESS, 42);
    REQUIRE(host1 != host2);
}

TEST_CASE("authority  inequality  ipv6 ipv6  false", "[authority  inequality]") {
    const authority host1("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    const authority host2("[" KI_AUTHORITY_IPV6_COMPRESSED_ADDRESS "]");
    REQUIRE( ! (host1 != host2));
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: authority  construct

#if ! defined(__EMSCRIPTEN__)
TEST_CASE("authority  construct  bogus ip  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("bogus");}(), invalid_option_value);
}

TEST_CASE("authority should throw invalid option exception for invalid IPv4", "[authority construct]") {
    REQUIRE_THROWS_AS([](){authority host("999.999.999.999");}(), invalid_option_value);
}

TEST_CASE("authority should throw invalid option exception for invalid IPv6", "[authority construct]") {
    REQUIRE_THROWS_AS([](){authority host("[:::]");}(), invalid_option_value);
}

TEST_CASE("authority  construct  invalid port  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("[::]:12345678901");}(), invalid_option_value);
}
#else
TEST_CASE("authority  construct  bogus ip  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("bogus");}(), std::invalid_argument);
}

TEST_CASE("authority  construct  invalid ipv4  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("999.999.999.999");}(), std::invalid_argument);
}

TEST_CASE("authority  construct  invalid ipv6  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("[:::]");}(), std::invalid_argument);
}

TEST_CASE("authority  construct  invalid port  throws invalid option", "[authority  construct]") {
    REQUIRE_THROWS_AS([](){authority host("[::]:12345678901");}(), std::invalid_argument);
}
#endif

// End Test Suite

// End Test Suite
