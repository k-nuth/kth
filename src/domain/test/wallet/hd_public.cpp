// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/domain/wallet/hd_private.hpp>
#include <kth/domain/wallet/hd_public.hpp>
#include <kth/domain/wallet/mnemonic.hpp>

using namespace kth;
using namespace kth::domain::wallet;

// Start Test Suite: hd public tests

// TODO: test altchain

// Compile-time verification that `from_verified_components`, the
// private ctor it wraps, and the three accessors are usable in a
// constant expression. A regression that made any of them
// runtime-only would surface as a compile error here.
namespace {

constexpr ec_compressed kSamplePoint = {{
    0x02, 0x50, 0x86, 0x3a, 0xd6, 0x4a, 0x87, 0xae,
    0x8a, 0x2f, 0xe8, 0x3c, 0x1a, 0xf1, 0xa8, 0x40,
    0x3c, 0xb5, 0x3f, 0x53, 0xe4, 0x86, 0xd8, 0x51,
    0x1d, 0xad, 0x8a, 0x04, 0x88, 0x7e, 0x5b, 0x23,
    0x52,
}};

constexpr hd_chain_code kSampleChain = {{
    0x87, 0x3d, 0xff, 0x81, 0xc0, 0x2f, 0x52, 0x56,
    0x23, 0xfd, 0x1f, 0xe5, 0x16, 0x7e, 0xac, 0x3a,
    0x55, 0xa0, 0x49, 0xde, 0x3d, 0x31, 0x4b, 0xb4,
    0x2e, 0xe2, 0x27, 0xff, 0xed, 0x37, 0xd5, 0x08,
}};

constexpr hd_lineage kSampleLineage{
    .prefixes = hd_public::mainnet,
    .depth = 0,
    .parent_fingerprint = 0,
    .child_number = 0,
};

constexpr auto kSample = hd_public::from_verified_components(
    kSamplePoint, kSampleChain, kSampleLineage);

static_assert(kSample.point() == kSamplePoint);
static_assert(kSample.chain_code() == kSampleChain);
static_assert(kSample.lineage() == kSampleLineage);
static_assert(kSample == kSample);
static_assert(hd_public::mainnet == 76067358);
static_assert(hd_public::testnet == 70617039);
static_assert(hd_public::to_prefix(0x1122334455667788ULL) == 0x55667788);

// hd_private: `from_verified_parts(hd_public, ec_secret)` mirrors
// hd_public's `from_verified_components` — takes an already-validated
// pair and wraps it. Constexpr all the way through.
constexpr ec_secret kSampleSecret = {{
    0xa2, 0xbc, 0xa8, 0x37, 0x27, 0x86, 0x7f, 0x39,
    0x76, 0x7e, 0x18, 0x0c, 0x7e, 0x9f, 0xd4, 0x84,
    0xef, 0x99, 0x6e, 0xe3, 0x37, 0x9a, 0xf3, 0x4b,
    0xd2, 0x8f, 0x60, 0xe1, 0xa6, 0x36, 0x93, 0x8c,
}};

constexpr auto kSamplePriv = hd_private::from_verified_parts(kSample, kSampleSecret);
static_assert(kSamplePriv.secret() == kSampleSecret);
static_assert(kSamplePriv.public_key() == kSample);
static_assert(kSamplePriv.point() == kSamplePoint);
static_assert(kSamplePriv.chain_code() == kSampleChain);
static_assert(kSamplePriv.lineage() == kSampleLineage);
static_assert(kSamplePriv == kSamplePriv);
static_assert(hd_private::mainnet == to_prefixes(76066276, hd_public::mainnet));
static_assert(hd_private::to_prefix(0x1122334455667788ULL) == 0x11223344);

} // namespace

constexpr char short_seed[] = "000102030405060708090a0b0c0d0e0f";
constexpr char long_seed[] = "fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542";

TEST_CASE("hd public derive public invalid false", "[hd public tests]") {
    auto const seed = decode_base16(short_seed);
    REQUIRE(seed);

    auto const m = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(m);
    hd_public const m_pub = m->to_public();
    REQUIRE( ! m_pub.derive_public(hd_first_hardened_key));
}

TEST_CASE("hd public encoded round trip expected", "[hd public tests]") {
    static auto const encoded = "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";
    auto const key = hd_public::parse_from(encoded);
    REQUIRE(key);
    REQUIRE(key->to_string() == encoded);
}

TEST_CASE("hd public derive public short seed expected", "[hd public tests]") {
    auto const seed = decode_base16(short_seed);
    REQUIRE(seed);

    auto const m = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(m);
    auto const m0h = m->derive_private(hd_first_hardened_key).value();
    auto const m0h1 = m0h.derive_private(1).value();

    hd_public const m_pub = m->to_public();
    auto const m0h_pub = m->derive_public(hd_first_hardened_key).value();
    auto const m0h1_pub = m0h_pub.derive_public(1).value();
    auto const m0h12h_pub = m0h1.derive_public(2 + hd_first_hardened_key).value();
    auto const m0h12h2_pub = m0h12h_pub.derive_public(2).value();
    auto const m0h12h2x_pub = m0h12h2_pub.derive_public(1000000000).value();

    REQUIRE(m_pub.to_string() == "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");
    REQUIRE(m0h_pub.to_string() == "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw");
    REQUIRE(m0h1_pub.to_string() == "xpub6ASuArnXKPbfEwhqN6e3mwBcDTgzisQN1wXN9BJcM47sSikHjJf3UFHKkNAWbWMiGj7Wf5uMash7SyYq527Hqck2AxYysAA7xmALppuCkwQ");
    REQUIRE(m0h12h_pub.to_string() == "xpub6D4BDPcP2GT577Vvch3R8wDkScZWzQzMMUm3PWbmWvVJrZwQY4VUNgqFJPMM3No2dFDFGTsxxpG5uJh7n7epu4trkrX7x7DogT5Uv6fcLW5");
    REQUIRE(m0h12h2_pub.to_string() == "xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV");
    REQUIRE(m0h12h2x_pub.to_string() == "xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy");
}

TEST_CASE("hd public derive public long seed expected", "[hd public tests]") {
    auto const seed = decode_base16(long_seed);
    REQUIRE(seed);

    auto const m = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(m);
    auto const m0 = m->derive_private(0).value();
    auto const m0xH = m0.derive_private(2147483647 + hd_first_hardened_key).value();
    auto const m0xH1 = m0xH.derive_private(1).value();

    hd_public const m_pub = m->to_public();
    auto const m0_pub = m_pub.derive_public(0).value();
    auto const m0xH_pub = m0.derive_public(2147483647 + hd_first_hardened_key).value();
    auto const m0xH1_pub = m0xH_pub.derive_public(1).value();
    auto const m0xH1yH_pub = m0xH1.derive_public(2147483646 + hd_first_hardened_key).value();
    auto const m0xH1yH2_pub = m0xH1yH_pub.derive_public(2).value();

    REQUIRE(m_pub.to_string() == "xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB");
    REQUIRE(m0_pub.to_string() == "xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH");
    REQUIRE(m0xH_pub.to_string() == "xpub6ASAVgeehLbnwdqV6UKMHVzgqAG8Gr6riv3Fxxpj8ksbH9ebxaEyBLZ85ySDhKiLDBrQSARLq1uNRts8RuJiHjaDMBU4Zn9h8LZNnBC5y4a");
    REQUIRE(m0xH1_pub.to_string() == "xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvmdMVCQcoNJxGoWaU9DCWh89LojfZ537wTfunKau47EL2dhHKon");
    REQUIRE(m0xH1yH_pub.to_string() == "xpub6ERApfZwUNrhLCkDtcHTcxd75RbzS1ed54G1LkBUHQVHQKqhMkhgbmJbZRkrgZw4koxb5JaHWkY4ALHY2grBGRjaDMzQLcgJvLJuZZvRcEL");
    REQUIRE(m0xH1yH2_pub.to_string() == "xpub6FnCn6nSzZAw5Tw7cgR9bi15UV96gLZhjDstkXXxvCLsUXBGXPdSnLFbdpq8p9HmGsApME5hQTZ3emM2rnY5agb9rXpVGyy3bdW6EEgAtqt");
}

// End Test Suite
