// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: stealth address tests

constexpr char scan_key[] = "03d9e876028f4fc062c19f7097762e4affc2ce4edfffa7d42e3c17cd157ec6d1bc";
constexpr char spend_key1[] = "0215a49b55a2ed2a02569cb6c018644211d408caab3aca86d2cc7d6a9e5789b1d2";
constexpr char stealth_address_encoded[] = "vJmzLu29obZcUGXXgotapfQLUpz7dfnZpbr4xg1R75qctf8xaXAteRdi3ZUk3T2ZMSad5KyPbve7uyH6eswYAxLHRVSbWgNUeoGuXp";

TEST_CASE("stealth address construct string expected encoding", "[stealth address]") {
    auto const scan = decode_base16<ec_compressed_size>(scan_key);
    REQUIRE(scan);
    auto const spend1 = decode_base16<ec_compressed_size>(spend_key1);
    REQUIRE(spend1);
    stealth_address address({}, *scan, {*spend1}, 0, 42);
    REQUIRE(address);
    REQUIRE(address.encoded() == stealth_address_encoded);
}

TEST_CASE("stealth address construct decoded expected properties", "[stealth address]") {
    stealth_address address(stealth_address_encoded);
    REQUIRE(address);
    REQUIRE((size_t)address.version() == 42);
    REQUIRE(encode_base16(address.scan_key()) == scan_key);
    REQUIRE(address.spend_keys().size() == 1u);
    REQUIRE(encode_base16(address.spend_keys()[0]) == spend_key1);
    REQUIRE((size_t)address.signatures() == 1u);
    REQUIRE(address.filter().size() == 0u);
    REQUIRE(address.encoded() == stealth_address_encoded);
}

TEST_CASE("stealth address encoding scan mainnet round trips", "[stealth address]") {
    auto const encoded = "vJmzLu29obZcUGXXgotapfQLUpz7dfnZpbr4xg1R75qctf8xaXAteRdi3ZUk3T2ZMSad5KyPbve7uyH6eswYAxLHRVSbWgNUeoGuXp";
    stealth_address address(encoded);
    REQUIRE(address.encoded() == encoded);
    REQUIRE(address.version() == 42u);
}

TEST_CASE("stealth address encoding scan testnet round trips", "[stealth address]") {
    std::string const encoded = "waPXhQwQE9tDugfgLkvpDs3dnkPx1RsfDjFt4zBq7EeWeATRHpyQpYrFZR8T4BQy91Vpvshm2TDER8b9ZryuZ8VSzz8ywzNzX8NqF4";
    stealth_address address(encoded);
    REQUIRE(address.encoded() == encoded);
    REQUIRE(address.version() == 43u);
}

TEST_CASE("stealth address encoding scan pub mainnet round trips", "[stealth address]") {
    auto const encoded = "hfFGUXFPKkQ5M6LC6aEUKMsURdhw93bUdYdacEtBA8XttLv7evZkira2i";
    stealth_address address(encoded);
    REQUIRE(address.encoded() == encoded);
    REQUIRE(address.version() == 42u);
}

TEST_CASE("stealth address encoding scan pub testnet round trip", "[stealth address]") {
    auto const encoded = "idPayBqZUpZH7Y5GTaoEyGxDsEmU377JUmhtqG8yoHCkfGfhnAHmGUJbL";
    stealth_address address(encoded);
    REQUIRE(address.encoded() == encoded);
    REQUIRE(address.version() == 43u);
}

// End Test Suite
