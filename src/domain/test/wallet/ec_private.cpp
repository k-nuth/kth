// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: ec private tests

// TODO(legacy): add version tests

// Compile-time verification that the constexpr surface is actually
// usable as a constant expression. A `constexpr` variable definition
// forces evaluation at translation time — a regression that made
// `from_verified_secret`, the private ctor, or any accessor
// runtime-only would surface as a compile error here, closer to the
// type than a downstream call site.
namespace {

constexpr ec_secret kSecret = {{
    0x80, 0x10, 0xb1, 0xbb, 0x11, 0x9a, 0xd3, 0x7d,
    0x4b, 0x65, 0xa1, 0x02, 0x2a, 0x31, 0x48, 0x97,
    0xb1, 0xb3, 0x61, 0x4b, 0x34, 0x59, 0x74, 0x33,
    0x2c, 0xb1, 0xb9, 0x58, 0x2c, 0xf0, 0x35, 0x36,
}};

constexpr auto kSample = ec_private::from_verified_secret(
    kSecret, ec_private::mainnet, /*compress=*/true);

static_assert(kSample.compressed());
static_assert(kSample.secret() == kSecret);
static_assert(kSample.version() == ec_private::mainnet);
static_assert(kSample.payment_version() == ec_private::mainnet_p2kh);
static_assert(kSample.wif_version() == ec_private::mainnet_wif);
static_assert(kSample == kSample);
static_assert(ec_private::compressed_sentinel == 0x01);
static_assert(ec_private::to_version(0x00, 0x80) == 0x8000);
static_assert(ec_private::to_address_prefix(0x8042) == 0x42);
static_assert(ec_private::to_wif_prefix(0x8042)     == 0x80);

} // namespace

constexpr auto secret = "8010b1bb119ad37d4b65a1022a314897b1b3614b345974332cb1b9582cf03536"_base16;
constexpr auto wif_compressed_str = "L1WepftUBemj6H4XQovkiW1ARVjxMqaw4oj2kmkYqdG1xTnBcHfC";
constexpr auto wif_uncompressed_str = "5JngqQmHagNTknnCshzVUysLMWAjT23FWs1TgNU5wyFH5SB3hrP";

TEST_CASE("ec private compressed wif compressed test", "[ec private]") {
    auto const key = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh);
    REQUIRE(key);
    REQUIRE(key->compressed());
}

TEST_CASE("ec private uncompressed wif not compressed test", "[ec private]") {
    auto const key = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh);
    REQUIRE(key);
    REQUIRE( ! key->compressed());
}

TEST_CASE("ec private encode wif compressed test", "[ec private]") {
    REQUIRE(ec_private::from_verified_secret(secret, ec_private::mainnet, true).to_string() == wif_compressed_str);
}

TEST_CASE("ec private encode wif uncompressed test", "[ec private]") {
    REQUIRE(ec_private::from_verified_secret(secret, 0x8000, false).to_string() == wif_uncompressed_str);
}

TEST_CASE("ec private decode wif compressed test", "[ec private]") {
    auto const priv = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh);
    REQUIRE(priv);
    REQUIRE(priv->secret() == secret);
    REQUIRE(priv->version() == 0x8000);
    REQUIRE(priv->compressed());
}

TEST_CASE("ec private decode wif uncompressed test", "[ec private]") {
    auto const priv = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh);
    REQUIRE(priv);
    REQUIRE(priv->secret() == secret);
    REQUIRE(priv->version() == 0x8000);
    REQUIRE( ! priv->compressed());
}

// End Test Suite
