// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

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
