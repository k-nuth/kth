// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

TEST_CASE("endpoint accepts only validated overlay peer names", "[endpoint][i2p][onion]") {
    std::string const i2p = std::string(52, 'a') + ".b32.i2p";
    auto const peer = endpoint::parse_from(i2p + ":0");
    REQUIRE(peer);
    REQUIRE(peer->host() == i2p);
    REQUIRE(peer->port() == 0);
    REQUIRE( ! endpoint::parse_from("invalid.b32.i2p:0"));
    REQUIRE( ! endpoint::parse_from("invalid.onion:8333"));
    REQUIRE( ! endpoint::parse_from("INVALID.ONION:8333"));
    REQUIRE( ! endpoint::parse_from("example.i2p:0"));
    REQUIRE( ! endpoint::parse_from(i2p + ".:0"));
    REQUIRE( ! endpoint::parse_from(i2p + "..:0"));
    REQUIRE( ! endpoint::parse_from("duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion.:8333"));
}

TEST_CASE("endpoint constructor accepts host and port", "[endpoint]") {
    endpoint const host{"foo", 0};
    REQUIRE(host.scheme() == "");
    REQUIRE(host.host() == "foo");
    REQUIRE(host.port() == 0u);
}

TEST_CASE("endpoint::parse_from extracts host and port", "[endpoint]") {
    auto const ep = endpoint::parse_from("foo.bar:42");
    REQUIRE(ep.has_value());
    REQUIRE(ep->scheme() == "");
    REQUIRE(ep->host() == "foo.bar");
    REQUIRE(ep->port() == 42u);
}

TEST_CASE("endpoint::parse_from extracts scheme, host, and port", "[endpoint]") {
    auto const ep = endpoint::parse_from("tcp://foo.bar:42");
    REQUIRE(ep.has_value());
    REQUIRE(ep->scheme() == "tcp");
    REQUIRE(ep->host() == "foo.bar");
    REQUIRE(ep->port() == 42u);
}

TEST_CASE("endpoint::parse_from extracts scheme and host without port", "[endpoint]") {
    auto const ep = endpoint::parse_from("tcp://foo.bar");
    REQUIRE(ep.has_value());
    REQUIRE(ep->scheme() == "tcp");
    REQUIRE(ep->host() == "foo.bar");
    REQUIRE(ep->port() == 0u);
}

// ---- error cases ----------------------------------------------------------

TEST_CASE("endpoint::parse_from rejects an empty string", "[endpoint]") {
    REQUIRE( ! endpoint::parse_from("").has_value());
}

TEST_CASE("endpoint::parse_from rejects a bare scheme with no host", "[endpoint]") {
    REQUIRE( ! endpoint::parse_from("tcp://").has_value());
}

TEST_CASE("endpoint::parse_from rejects a port with no host", "[endpoint]") {
    REQUIRE( ! endpoint::parse_from(":42").has_value());
}

TEST_CASE("endpoint::parse_from rejects a port that overflows uint16_t", "[endpoint]") {
    REQUIRE( ! endpoint::parse_from("foo:70000").has_value());
}

TEST_CASE("endpoint::parse_from rejects non-numeric port", "[endpoint]") {
    REQUIRE( ! endpoint::parse_from("foo:bar").has_value());
}

// A link-local peer has a scoped address, whose text the parser does not take.
TEST_CASE("endpoint from an authority keeps a scoped IPv6 address", "[endpoint]") {
    auto address = ::asio::ip::make_address_v6("fe80::1");
    address.scope_id(1);
    infrastructure::config::authority const scoped(::asio::ip::address(address), 8333);

    infrastructure::config::endpoint const wrapped(scoped);
    CHECK(wrapped.host() == scoped.to_hostname());
    CHECK(wrapped.port() == 8333);
    CHECK(wrapped.scheme().empty());
}

TEST_CASE("endpoint from an authority matches the parsed text of a plain address", "[endpoint]") {
    auto const numeric = infrastructure::config::authority::parse_from("192.0.2.1:8333");
    REQUIRE(numeric.has_value());
    auto const parsed = infrastructure::config::endpoint::parse_from("192.0.2.1:8333");
    REQUIRE(parsed.has_value());

    infrastructure::config::endpoint const wrapped(*numeric);
    CHECK(wrapped.host() == parsed->host());
    CHECK(wrapped.port() == parsed->port());
}
