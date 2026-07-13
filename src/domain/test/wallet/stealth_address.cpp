// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: stealth address tests

// Compile-time verification that the four `static constexpr` byte /
// size constants can be evaluated in a constant expression. A
// regression that pushed them back into the `.cpp` (as pre-#NNN
// definitions) would surface as a compile error here.
static_assert(stealth_address::mainnet_p2kh == 0x2a);
static_assert(stealth_address::reuse_key_flag == 1U);
static_assert(stealth_address::min_filter_bits == byte_bits);
static_assert(stealth_address::max_filter_bits == sizeof(uint32_t) * byte_bits);

constexpr char scan_key[] = "03d9e876028f4fc062c19f7097762e4affc2ce4edfffa7d42e3c17cd157ec6d1bc";
constexpr char spend_key1[] = "0215a49b55a2ed2a02569cb6c018644211d408caab3aca86d2cc7d6a9e5789b1d2";
constexpr char stealth_address_encoded[] = "vJmzLu29obZcUGXXgotapfQLUpz7dfnZpbr4xg1R75qctf8xaXAteRdi3ZUk3T2ZMSad5KyPbve7uyH6eswYAxLHRVSbWgNUeoGuXp";

TEST_CASE("stealth address construct string expected encoding", "[stealth address]") {
    auto const scan = decode_base16<ec_compressed_size>(scan_key);
    REQUIRE(scan);
    auto const spend1 = decode_base16<ec_compressed_size>(spend_key1);
    REQUIRE(spend1);
    auto const address = stealth_address::from_components({}, *scan, {*spend1}, 0, 42);
    REQUIRE(address);
    REQUIRE(address->to_string() == stealth_address_encoded);
}

TEST_CASE("stealth address construct decoded expected properties", "[stealth address]") {
    auto const address = stealth_address::parse_from(stealth_address_encoded);
    REQUIRE(address);
    REQUIRE((size_t)address->version() == 42);
    REQUIRE(encode_base16(address->scan_key()) == scan_key);
    REQUIRE(address->spend_keys().size() == 1u);
    REQUIRE(encode_base16(address->spend_keys()[0]) == spend_key1);
    REQUIRE((size_t)address->signatures() == 1u);
    REQUIRE(address->filter().size() == 0u);
    REQUIRE(address->to_string() == stealth_address_encoded);
}

TEST_CASE("stealth address encoding scan mainnet round trips", "[stealth address]") {
    auto const encoded = "vJmzLu29obZcUGXXgotapfQLUpz7dfnZpbr4xg1R75qctf8xaXAteRdi3ZUk3T2ZMSad5KyPbve7uyH6eswYAxLHRVSbWgNUeoGuXp";
    auto const address = stealth_address::parse_from(encoded);
    REQUIRE(address);
    REQUIRE(address->to_string() == encoded);
    REQUIRE(address->version() == 42u);
}

TEST_CASE("stealth address encoding scan testnet round trips", "[stealth address]") {
    std::string const encoded = "waPXhQwQE9tDugfgLkvpDs3dnkPx1RsfDjFt4zBq7EeWeATRHpyQpYrFZR8T4BQy91Vpvshm2TDER8b9ZryuZ8VSzz8ywzNzX8NqF4";
    auto const address = stealth_address::parse_from(encoded);
    REQUIRE(address);
    REQUIRE(address->to_string() == encoded);
    REQUIRE(address->version() == 43u);
}

TEST_CASE("stealth address encoding scan pub mainnet round trips", "[stealth address]") {
    auto const encoded = "hfFGUXFPKkQ5M6LC6aEUKMsURdhw93bUdYdacEtBA8XttLv7evZkira2i";
    auto const address = stealth_address::parse_from(encoded);
    REQUIRE(address);
    REQUIRE(address->to_string() == encoded);
    REQUIRE(address->version() == 42u);
}

TEST_CASE("stealth address encoding scan pub testnet round trip", "[stealth address]") {
    auto const encoded = "idPayBqZUpZH7Y5GTaoEyGxDsEmU377JUmhtqG8yoHCkfGfhnAHmGUJbL";
    auto const address = stealth_address::parse_from(encoded);
    REQUIRE(address);
    REQUIRE(address->to_string() == encoded);
    REQUIRE(address->version() == 43u);
}

TEST_CASE("stealth address parse_from empty string fails", "[stealth address]") {
    REQUIRE( ! stealth_address::parse_from(""));
}

TEST_CASE("stealth address parse_from garbage fails", "[stealth address]") {
    REQUIRE( ! stealth_address::parse_from("not-a-stealth-address"));
}

// End Test Suite
