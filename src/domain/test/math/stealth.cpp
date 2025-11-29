// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// TODO(legacy): split out individual functions and standardize test names.
// Start Test Suite: stealth tests

constexpr char scan_private_hex[] = "fa63521e333e4b9f6a98a142680d3aef4d8e7f79723ce0043691db55c36bd905";
constexpr char scan_public_hex[] = "034ea70b28d607bf3a2493102001cab35689cf2152530bf8bf8a5b594af6ae31d0";

constexpr char spend_private_hex[] = "dcc1250b51c0f03ae4e978e0256ede51dc1144e345c926262b9717b1bcc9bd1b";
constexpr char spend_public_hex[] = "03d5b3853bbee336b551ff999b0b1d656e65a7649037ae0dcb02b3c4ff5f29e5be";

constexpr char ephemeral_private_hex[] = "5f70a77b32260a7a32c62242381fba2cf40c0e209e665a7959418eae4f2da22b";
constexpr char ephemeral_public_hex[] = "0387ff9128d18ddcec0a8119589a62b88bc035cb9cd6db08ce5ff702a78ef8f922";

constexpr char stealth_private_hex[] = "280a9931c0a7b8f9bed96bad35f69a1431817fb77043fdff641ad48ce1e4411e";
constexpr char stealth_public_hex[] = "0305f6b99a44a2bdec8b484ffcee561cf9a0c3b7ea92ea8e6334e6fbc4f1c17899";

// $ bx ec-add 03d5b3853bbee336b551ff999b0b1d656e65a7649037ae0dcb02b3c4ff5f29e5be 4b4974266ee6c8bed9eff2cd1087bbc1101f17bad9c37814f8561b67f550c544 | bx ec-to-address
constexpr char p2pkh_address[] = "1Gvq8pSTRocNLDyf858o4PL3yhZm5qQDgB";

// $ bx ec-add 03d5b3853bbee336b551ff999b0b1d656e65a7649037ae0dcb02b3c4ff5f29e5be 4b4974266ee6c8bed9eff2cd1087bbc1101f17bad9c37814f8561b67f550c544 | bx ec-to-address - v 111
// #define p2pkh_address_TESTNET "mwSnRsXSEq3d7LTGqe7AtJYNqhATwHdhMb"

TEST_CASE("stealth round trip", "[stealth]") {
    auto const expected_stealth_private = decode_base16<ec_secret_size>(stealth_private_hex);
    REQUIRE(expected_stealth_private);

    // Receiver generates a new scan private.
    auto const scan_private = decode_base16<ec_secret_size>(scan_private_hex);
    REQUIRE(scan_private);
    ec_compressed scan_public;
    REQUIRE(secret_to_public(scan_public, *scan_private));
    REQUIRE(encode_base16(scan_public) == scan_public_hex);

    // Receiver generates a new spend private.
    auto const spend_private = decode_base16<ec_secret_size>(spend_private_hex);
    REQUIRE(spend_private);
    ec_compressed spend_public;
    REQUIRE(secret_to_public(spend_public, *spend_private));
    REQUIRE(encode_base16(spend_public) == spend_public_hex);

    // Sender generates a new ephemeral key.
    auto const ephemeral_private = decode_base16<ec_secret_size>(ephemeral_private_hex);
    REQUIRE(ephemeral_private);
    ec_compressed ephemeral_public;
    REQUIRE(secret_to_public(ephemeral_public, *ephemeral_private));
    REQUIRE(encode_base16(ephemeral_public) == ephemeral_public_hex);

    // Sender derives stealth public, requiring ephemeral private.
    ec_compressed sender_public;
    REQUIRE(uncover_stealth(sender_public, scan_public, *ephemeral_private, spend_public));
    REQUIRE(encode_base16(sender_public) == stealth_public_hex);

    // Receiver derives stealth public, requiring scan private.
    ec_compressed receiver_public;
    REQUIRE(uncover_stealth(receiver_public, ephemeral_public, *scan_private, spend_public));
    REQUIRE(encode_base16(receiver_public) == stealth_public_hex);

    // Only reciever can derive stealth private, as it requires both scan and spend private.
    ec_secret stealth_priv;
    REQUIRE(uncover_stealth(stealth_priv, ephemeral_public, *scan_private, *spend_private));

    // This shows that both parties have actually generated stealth public.
    ec_compressed stealth_pub;
    REQUIRE(secret_to_public(stealth_pub, stealth_priv));
    REQUIRE(encode_base16(stealth_pub) == stealth_public_hex);

    // Both parties therefore have the ability to generate the p2pkh address.
    // versioning: stealth_address::main corresponds to payment_address::main_p2pkh
    wallet::payment_address address(wallet::ec_public{stealth_pub}, wallet::payment_address::mainnet_p2kh);
    REQUIRE(address.encoded_legacy() == p2pkh_address);
}

TEST_CASE("verify string constructor", "[stealth]") {
    std::string const value = "01100110000";
    binary prefix(value);
    REQUIRE(value.size() == prefix.size());
    for (size_t i = 0; i < value.size(); ++i) {
        auto const comparison = value[i] == '1';
       REQUIRE(prefix[i] == comparison);
    }
}

// Binary as a value on the left, padded with zeros to the y.
TEST_CASE("compare constructor results", "[stealth]") {
    std::string value = "01100111000";
    binary prefix(value);
    data_chunk blocks{{0x67, 0x00}};
    binary prefix2(value.size(), blocks);
    REQUIRE(prefix == prefix2);
}

TEST_CASE("bitfield test1", "[stealth]") {
    binary prefix("01100111001");
    data_chunk raw_bitfield{{0x67, 0x20, 0x00, 0x0}};
    REQUIRE(raw_bitfield.size() * 8 >= prefix.size());
    binary compare(prefix.size(), raw_bitfield);
    REQUIRE(prefix == compare);
}

TEST_CASE("bitfield test2", "[stealth]") {
    data_chunk const blocks{{0x8b, 0xf4, 0x1c, 0x69}};
    const binary prefix(27, blocks);
    data_chunk const raw_bitfield{{0x8b, 0xf4, 0x1c, 0x79}};
    REQUIRE(raw_bitfield.size() * 8 >= prefix.size());
    const binary compare(prefix.size(), raw_bitfield);
    REQUIRE(prefix == compare);
}

TEST_CASE("bitfield test3", "[stealth]") {
    data_chunk const blocks{{0x69, 0x1c, 0xf4, 0x8b}};
    const binary prefix(32, blocks);
    data_chunk const raw_bitfield{{0x69, 0x1c, 0xf4, 0x8b}};
    const binary compare(prefix.size(), raw_bitfield);
    REQUIRE(prefix == compare);
}

TEST_CASE("bitfield test4", "[stealth]") {
    data_chunk const blocks{{0x69, 0x1c, 0xf4, 0x8b}};
    const binary prefix(29, blocks);
    data_chunk const raw_bitfield{{0x69, 0x1c, 0xf4, 0x8b}};
    const binary compare(prefix.size(), raw_bitfield);
    REQUIRE(prefix == compare);
}

// End Test Suite
