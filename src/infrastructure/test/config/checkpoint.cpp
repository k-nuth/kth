// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

constexpr char checkpoint_hash_a[] = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr char checkpoint_a[]      = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f:0";
constexpr char checkpoint_b[]      = "0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d:11111";

TEST_CASE("checkpoint::parse_from round-trips hash and height", "[checkpoint]") {
    auto const genesis = checkpoint::parse_from(checkpoint_b);
    REQUIRE(genesis.has_value());
    REQUIRE(genesis->height() == 11111u);
    REQUIRE(genesis->to_string() == checkpoint_b);
}

TEST_CASE("checkpoint::parse_from defaults height to zero when omitted", "[checkpoint]") {
    auto const genesis = checkpoint::parse_from(checkpoint_hash_a);
    REQUIRE(genesis.has_value());
    REQUIRE(genesis->height() == 0u);
    REQUIRE(genesis->to_string() == checkpoint_a);
}

TEST_CASE("checkpoint constructor accepts a hash_digest and height", "[checkpoint]") {
    hash_digest digest;
    kth::decode_hash(digest, checkpoint_hash_a);
    checkpoint const genesis{digest, 42};
    REQUIRE(genesis.height() == 42u);
    REQUIRE(encode_hash(genesis.hash()) == checkpoint_hash_a);
}

TEST_CASE("checkpoint::parse_from rejects malformed input", "[checkpoint]") {
    // Bogus hash (not 64 hex chars) with and without height.
    REQUIRE( ! checkpoint::parse_from("bogus:42").has_value());
    REQUIRE( ! checkpoint::parse_from("bogus").has_value());

    // Valid hash but non-numeric height.
    REQUIRE( ! checkpoint::parse_from(std::string{checkpoint_hash_a} + ":xxx").has_value());

    // Valid hash but height >2^64 (21 digits — grammar rejects >20 digits).
    REQUIRE( ! checkpoint::parse_from(std::string{checkpoint_hash_a} + ":1000000000100000000001").has_value());

    // Valid hash + numeric height that overflows uint64_t (regex allows the
    // 20-digit number, but from_chars flags out-of-range).
    REQUIRE( ! checkpoint::parse_from(std::string{checkpoint_hash_a} + ":18446744073709551616").has_value());
}
