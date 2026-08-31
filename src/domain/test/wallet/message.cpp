// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// $ bx base16-encode "Satoshi" | bx sha256
constexpr auto secret = "002688cc350a5333a87fa622eacec626c3d1c0ebf9f3793de3885fa254d7e393"_base16;
constexpr auto signature_compressed = "20c0ae26619db18abd1e8a84d005bafd336512eda7207cf7f4f6c36c9614ed6bcf531a954929ddc0a86578f4d28a26e19b676c890a49881d6f25e393befd6d1682"_base16;
constexpr auto signature_uncompressed = "1c3484d71301fbdd9eec713894add25867663d9a91d637682f09179a211d16a1f26068178de890a0117df61c436e9062f87ae1790579829caae2911833ba9e35b0"_base16;

// WIF keys also used in WIF test vectors.
constexpr auto wif_compressed_str = "L1WepftUBemj6H4XQovkiW1ARVjxMqaw4oj2kmkYqdG1xTnBcHfC";
constexpr auto wif_uncompressed_str = "5JngqQmHagNTknnCshzVUysLMWAjT23FWs1TgNU5wyFH5SB3hrP";
constexpr auto signature_wif_compressed = "20813288c5d9e3a56a297758df28bec5ffe4ceb107ac66f1d215c156ecb7845ca65efb7a84a5267edc77538479ccb01efdb006837d35e246b2cacae22acb4c6e46"_base16;
constexpr auto signature_wif_uncompressed = "1b25c35d61aa2ff5353efc36d747ed3ef179bc0f5b3d1c60f0617006c9015a0d8271da05103b987d1c26a2ecb053a56ce885805cbacefa230e69bfe18727ea4a04"_base16;

// Generated using Electrum and above SECRET (compressed):
constexpr auto electrum_signature = "1f1429ddc5e03888411065e4b36eec7de4901d580d51e6209798b9c06fdd39461a4884679f35d1e8d7321fe01f3401ed916732383f6b5f8a688ea9ae4321fbf4ae";  // for decode_base16

// Start Test Suite: message tests

// Start Test Suite: message recovery magic

TEST_CASE("message recovery id to magic uncompressed valid expected", "[message recovery magic]") {
    uint8_t out_magic;
    REQUIRE(recovery_id_to_magic(out_magic, 0, false));
    REQUIRE(out_magic == 0x1b);
    REQUIRE(recovery_id_to_magic(out_magic, 1, false));
    REQUIRE(out_magic == 0x1c);
    REQUIRE(recovery_id_to_magic(out_magic, 2, false));
    REQUIRE(out_magic == 0x1d);
    REQUIRE(recovery_id_to_magic(out_magic, 3, false));
    REQUIRE(out_magic == 0x1e);
}

TEST_CASE("message recovery id to magic compressed valid expected", "[message recovery magic]") {
    uint8_t out_magic;
    REQUIRE(recovery_id_to_magic(out_magic, 0, true));
    REQUIRE(out_magic == 0x1f);
    REQUIRE(recovery_id_to_magic(out_magic, 1, true));
    REQUIRE(out_magic == 0x20);
    REQUIRE(recovery_id_to_magic(out_magic, 2, true));
    REQUIRE(out_magic == 0x21);
    REQUIRE(recovery_id_to_magic(out_magic, 3, true));
    REQUIRE(out_magic == 0x22);
}

TEST_CASE("message magic to recovery id uncompressed expected", "[message recovery magic]") {
    bool out_compressed = true;
    uint8_t out_recovery_id = 0xff;
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x1b));
    REQUIRE( ! out_compressed);
    REQUIRE(out_recovery_id == 0u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x1c));
    REQUIRE( ! out_compressed);
    REQUIRE(out_recovery_id == 1u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x1d));
    REQUIRE( ! out_compressed);
    REQUIRE(out_recovery_id == 2u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x1e));
    REQUIRE( ! out_compressed);
    REQUIRE(out_recovery_id == 3u);
}

TEST_CASE("message magic to recovery id compressed expected", "[message recovery magic]") {
    bool out_compressed = false;
    uint8_t out_recovery_id = 0xff;
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x1f));
    REQUIRE(out_compressed);
    REQUIRE(out_recovery_id == 0u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x20));
    REQUIRE(out_compressed);
    REQUIRE(out_recovery_id == 1u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x21));
    REQUIRE(out_compressed);
    REQUIRE(out_recovery_id == 2u);
    REQUIRE(magic_to_recovery_id(out_recovery_id, out_compressed, 0x22));
    REQUIRE(out_compressed);
    REQUIRE(out_recovery_id == 3u);
}

TEST_CASE("message recovery id to magic uncompressed invalid false", "[message recovery magic]") {
    uint8_t out_magic;
    REQUIRE( ! recovery_id_to_magic(out_magic, 4, false));
    REQUIRE( ! recovery_id_to_magic(out_magic, max_uint8, false));
}

TEST_CASE("message recovery id to magic compressed invalid false", "[message recovery magic]") {
    uint8_t out_magic;
    REQUIRE( ! recovery_id_to_magic(out_magic, 4, true));
    REQUIRE( ! recovery_id_to_magic(out_magic, max_uint8, true));
}

TEST_CASE("message magic to recovery id invalid false", "[message recovery magic]") {
    bool out_compressed;
    uint8_t out_recovery_id;
    REQUIRE( ! magic_to_recovery_id(out_recovery_id, out_compressed, 0));
    REQUIRE( ! magic_to_recovery_id(out_recovery_id, out_compressed, max_uint8));
}

// End Test Suite

// Start Test Suite: message sign message

TEST_CASE("message sign message compressed expected", "[message sign message]") {
    auto const compressed = true;
    auto const address = payment_address::from_ec_private(ec_private::from_verified_secret(secret, 0x00, compressed)).value();
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, secret, compressed));
    REQUIRE(out_signature == signature_compressed);
}

TEST_CASE("message sign message uncompressed expected", "[message sign message]") {
    auto const compressed = false;
    auto const address = payment_address::from_ec_private(ec_private::from_verified_secret(secret, 0x00, compressed)).value();
    auto const message = to_chunk(std::string("Uncompressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, secret, compressed));
    REQUIRE(out_signature == signature_uncompressed);
}

TEST_CASE("message sign message secret compressed expected", "[message sign message]") {
    auto const priv = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh).value();
    auto const address = payment_address::from_ec_private(priv).value();
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv));
    REQUIRE(out_signature == signature_wif_compressed);
}

TEST_CASE("message sign message wif compressed expected", "[message sign message]") {
    auto const priv = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh).value();
    auto const address = payment_address::from_ec_private(priv).value();
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv.secret(), priv.compressed()));
    REQUIRE(out_signature == signature_wif_compressed);
}

TEST_CASE("message sign message wif uncompressed expected", "[message sign message]") {
    auto const priv = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh).value();
    auto const address = payment_address::from_ec_private(priv).value();
    auto const message = to_chunk(std::string("Uncompressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv.secret(), priv.compressed()));
    REQUIRE(out_signature == signature_wif_uncompressed);
}

// End Test Suite

// Start Test Suite: message verify message

TEST_CASE("message verify message compressed expected", "[message verify message]") {
    auto const address = payment_address::from_ec_private(ec_private::from_verified_secret(secret, ec_private::mainnet, true)).value();
    auto const message = to_chunk(std::string("Compressed"));
    REQUIRE(verify_message(message, address, signature_compressed));
}

TEST_CASE("message verify message uncompressed expected", "[message verify message]") {
    auto const address = payment_address::from_ec_private(ec_private::from_verified_secret(secret, 0x00, false)).value();
    auto const message = to_chunk(std::string("Uncompressed"));
    REQUIRE(verify_message(message, address, signature_uncompressed));
}

TEST_CASE("message verify message wif compressed round trip", "[message verify message]") {
    auto const priv = ec_private::parse_from(wif_compressed_str, ec_private::mainnet_p2kh).value();
    auto const address = payment_address::from_ec_private(priv).value();
    auto const message = to_chunk(std::string("Compressed"));
    REQUIRE(verify_message(message, address, signature_wif_compressed));
}

TEST_CASE("message verify message wif uncompressed round trip", "[message verify message]") {
    auto const priv = ec_private::parse_from(wif_uncompressed_str, ec_private::mainnet_p2kh).value();
    auto const address = payment_address::from_ec_private(priv).value();
    auto const message = to_chunk(std::string("Uncompressed"));
    REQUIRE(verify_message(message, address, signature_wif_uncompressed));
}

TEST_CASE("message verify message electrum compressed okay", "[message verify message]") {
    auto const sig = decode_base16<message_signature_size>(electrum_signature);
    REQUIRE(sig);

    // Address of the compressed public key of the message signer.
    auto const address = payment_address::parse_from("1PeChFbhxDD9NLbU21DfD55aQBC4ZTR3tE").value();
    auto const message = to_chunk(std::string("Nakomoto"));
    REQUIRE(verify_message(message, address, *sig));
}

TEST_CASE("message verify message electrum incorrect address false", "[message verify message]") {
    auto const sig = decode_base16<message_signature_size>(electrum_signature);
    REQUIRE(sig);

    // Address of the uncompressed public key of the message signer (incorrect).
    auto const address = payment_address::parse_from("1Em1SX7qQq1pTmByqLRafhL1ypx2V786tP").value();
    auto const message = to_chunk(std::string("Nakomoto"));
    REQUIRE( ! verify_message(message, address, *sig));
}

// End Test Suite

// End Test Suite
