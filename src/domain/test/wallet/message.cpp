// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// $ bx base16-encode "Satoshi" | bx sha256
constexpr auto secret = "002688cc350a5333a87fa622eacec626c3d1c0ebf9f3793de3885fa254d7e393"_base16;
constexpr auto signature_compressed = "1fa7b234dc5168e6ee3cab90c9cb5517dc851d18d5ac6e0b183daa9ed4aec8a1e752982ff04d0dce0f80d1d20ef68d31aeb000aca1313b9ca72c0acb1cea555ea4"_base16;
constexpr auto signature_uncompressed = "1c4f4780ab587f0a9011e2f5543acfb8628710a5c1edc6937f62a524bdbea65a9462e8f8ea2767b917382182d5fa498c2790b18bc4dbd2579dcc41703c880f6c3b"_base16;

// WIF keys also used in WIF test vectors.
constexpr auto wif_compressed_str = "L1WepftUBemj6H4XQovkiW1ARVjxMqaw4oj2kmkYqdG1xTnBcHfC";
constexpr auto wif_uncompressed_str = "5JngqQmHagNTknnCshzVUysLMWAjT23FWs1TgNU5wyFH5SB3hrP";
constexpr auto signature_wif_compressed = "1f667668be93a0be1f3cbb086a39326f3c82b2e6ef3fb98e370fe499e3a822111d23d82f01a4a6b1b9c6cfb9b2f01c4dde5e8a16b19b0be7d7452b6f8b29bc095d"_base16;
constexpr auto signature_wif_uncompressed = "1b1b6c97843dcea3f7b5557492c4da6bad50917be4d8e2cbde47e580e95abaff653f71ac8b796705811fc0e7f74492c3614f6ec0797146607fda4ce4252627c738"_base16;

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
    payment_address const address(ec_private{secret, 0x00, compressed});
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, secret, compressed));
    REQUIRE(out_signature == signature_compressed);
}

TEST_CASE("message sign message uncompressed expected", "[message sign message]") {
    auto const compressed = false;
    payment_address const address(ec_private{secret, 0x00, compressed});
    auto const message = to_chunk(std::string("Uncompressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, secret, compressed));
    REQUIRE(out_signature == signature_uncompressed);
}

TEST_CASE("message sign message secret compressed expected", "[message sign message]") {
    ec_private priv(wif_compressed_str);
    payment_address const address(priv);
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv));
    REQUIRE(out_signature == signature_wif_compressed);
}

TEST_CASE("message sign message wif compressed expected", "[message sign message]") {
    ec_private priv(wif_compressed_str);
    payment_address const address(priv);
    auto const message = to_chunk(std::string("Compressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv, priv.compressed()));
    REQUIRE(out_signature == signature_wif_compressed);
}

TEST_CASE("message sign message wif uncompressed expected", "[message sign message]") {
    ec_private priv(wif_uncompressed_str);
    payment_address const address(priv);
    auto const message = to_chunk(std::string("Uncompressed"));
    message_signature out_signature;
    REQUIRE(sign_message(out_signature, message, priv, priv.compressed()));
    REQUIRE(out_signature == signature_wif_uncompressed);
}

// End Test Suite

// Start Test Suite: message verify message

TEST_CASE("message verify message compressed expected", "[message verify message]") {
    payment_address const address(ec_private{secret});
    auto const message = to_chunk(std::string("Compressed"));
    REQUIRE(verify_message(message, address, signature_compressed));
}

TEST_CASE("message verify message uncompressed expected", "[message verify message]") {
    payment_address const address(ec_private{secret, 0x00, false});
    auto const message = to_chunk(std::string("Uncompressed"));
    REQUIRE(verify_message(message, address, signature_uncompressed));
}

TEST_CASE("message verify message wif compressed round trip", "[message verify message]") {
    ec_private priv(wif_compressed_str);
    payment_address const address(priv);
    auto const message = to_chunk(std::string("Compressed"));
    REQUIRE(verify_message(message, address, signature_wif_compressed));
}

TEST_CASE("message verify message wif uncompressed round trip", "[message verify message]") {
    ec_private priv(wif_uncompressed_str);
    payment_address const address(priv);
    auto const message = to_chunk(std::string("Uncompressed"));
    REQUIRE(verify_message(message, address, signature_wif_uncompressed));
}

TEST_CASE("message verify message electrum compressed okay", "[message verify message]") {
    auto const sig = decode_base16<message_signature_size>(electrum_signature);
    REQUIRE(sig);

    // Address of the compressed public key of the message signer.
    payment_address const address("1PeChFbhxDD9NLbU21DfD55aQBC4ZTR3tE");
    auto const message = to_chunk(std::string("Nakomoto"));
    REQUIRE(verify_message(message, address, *sig));
}

TEST_CASE("message verify message electrum incorrect address false", "[message verify message]") {
    auto const sig = decode_base16<message_signature_size>(electrum_signature);
    REQUIRE(sig);

    // Address of the uncompressed public key of the message signer (incorrect).
    payment_address const address("1Em1SX7qQq1pTmByqLRafhL1ypx2V786tP");
    auto const message = to_chunk(std::string("Nakomoto"));
    REQUIRE( ! verify_message(message, address, *sig));
}

// End Test Suite

// End Test Suite
