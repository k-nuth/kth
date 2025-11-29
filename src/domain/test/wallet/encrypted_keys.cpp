// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <cstddef>
#include <string>

#include <test_helpers.hpp>

#include <kth/infrastructure/formats/base_58.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: encrypted tests

#ifdef WITH_ICU

TEST_CASE("encrypted  fixture  unicode passphrase  matches encrypted test vector", "[encrypted]") {
    auto const encoded_password = "cf92cc8100f0909080f09f92a9"_base16;
    std::string passphrase(encoded_password.begin(), encoded_password.end());

    // This confirms that the passphrase decodes as expected in BIP38.
    auto const normal = to_normal_nfc_form(passphrase);
    data_chunk normalized(normal.size());
    std::copy_n(normal.begin(), normal.size(), normalized.begin());
    REQUIRE(encode_base16(normalized) == "cf9300f0909080f09f92a9");
}

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  create token lot

#define KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, bytes, lot, sequence) \
    encrypted_token out_token;                                        \
    REQUIRE(create_token(out_token, passphrase, bytes, lot, sequence))

TEST_CASE("encrypted  create token lot  lot overlow  false", "[encrypted  create token lot]") {
    size_t const lot = 1048575 + 1;
    size_t const sequence = 0;
    auto const passphrase = "";
    auto const salt = "baadf00d"_base16;
    encrypted_token out_token;
    REQUIRE( ! create_token(out_token, passphrase, salt, lot, sequence));
}

TEST_CASE("encrypted  create token lot  sequence overlow  false", "[encrypted  create token lot]") {
    size_t const lot = 0;
    size_t const sequence = 4095 + 1;
    auto const passphrase = "";
    auto const salt = "baadf00d"_base16;
    encrypted_token out_token;
    REQUIRE( ! create_token(out_token, passphrase, salt, lot, sequence));
}

TEST_CASE("encrypted  create token lot  defaults  expected", "[encrypted  create token lot]") {
    size_t const lot = 0;
    size_t const sequence = 0;
    auto const passphrase = "";
    auto const salt = "baadf00d"_base16;
    KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, salt, lot, sequence);
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8F7yQVcg1eQKPuX7rzGwBtEH1YSZnKbyk75x3rugZu1ci4RyF4rEn");
}

TEST_CASE("encrypted  create token lot  passphrase  expected", "[encrypted  create token lot]") {
    size_t const lot = 0;
    size_t const sequence = 0;
    auto const passphrase = "passphrase";
    auto const salt = "baadf00d"_base16;
    KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, salt, lot, sequence);
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8F7x4pQXMhsJs2j7L8LTV8ujk9jGqgzUrafBeto9VrabP5SmvANvz");
}

TEST_CASE("encrypted  create token lot  passphrase lot max  expected", "[encrypted  create token lot]") {
    size_t const lot = 1048575;
    size_t const sequence = 0;
    auto const passphrase = "passphrase";
    auto const salt = "baadf00d"_base16;
    KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, salt, lot, sequence);
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8FGWnwMTnTFiHSDnqyARArE2YSFQzMHtCZvM2oWg2K3Ua2crKyc11");
}

TEST_CASE("encrypted  create token lot  passphrase sequence max  expected", "[encrypted  create token lot]") {
    size_t const lot = 0;
    size_t const sequence = 4095;
    auto const passphrase = "passphrase";
    auto const salt = "baadf00d"_base16;
    KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, salt, lot, sequence);
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8FGWnwMTnTFiHSDnqyARArE2YSFQzMHtCZvM2oWg2K3Ua2crKyc11");
}

TEST_CASE("encrypted  create token lot  passphrase lot sequence  expected", "[encrypted  create token lot]") {
    size_t const lot = 42;
    size_t const sequence = 42;
    auto const passphrase = "passphrase";
    auto const salt = "baadf00d"_base16;
    KD_REQUIRE_CREATE_TOKEN_LOT(passphrase, salt, lot, sequence);
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8FGWnwMTnTFiHSDnqyARArE2YSFQzMHtCZvM2oWg2K3Ua2crKyc11");
}

// End Test Suite

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  create token entropy

#define KD_CREATE_TOKEN_ENTROPY(passphrase, bytes) \
    encrypted_token out_token;                     \
    create_token(out_token, passphrase, bytes)

TEST_CASE("encrypted  create token entropy  defaults  expected", "[encrypted  create token entropy]") {
    auto const passphrase = "";
    auto const entropy = "baadf00dbaadf00d"_base16;
    KD_CREATE_TOKEN_ENTROPY(passphrase, entropy);
    REQUIRE(encode_base58(out_token) == "passphraseqVHzjNrYRo5G6yLmJ7TQ49fKnQtsgjybNgNHAKBCQKoFZcTNjNJtg4oCUgtPt3");
}

TEST_CASE("encrypted  create token entropy  passphrase  expected", "[encrypted  create token entropy]") {
    auto const passphrase = "passphrase";
    auto const entropy = "baadf00dbaadf00d"_base16;
    KD_CREATE_TOKEN_ENTROPY(passphrase, entropy);
    REQUIRE(encode_base58(out_token) == "passphraseqVHzjNrYRo5G6sfRB4YdSaQ2m8URnkBYS1UT6JBju5G5o45YRZKLDpK6J3PEGq");
}

// End Test Suite

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  encrypt private

#define KD_REQUIRE_ENCRYPT(secret, passphrase, version, compressed, expected)     \
    encrypted_private out_private;                                                \
    REQUIRE(encrypt(out_private, secret, passphrase, version, compressed));
    REQUIRE(encode_base58(out_private) == expected)

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  encrypt private  vector 0  expected", "[encrypted  encrypt private]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const expected = "6PRVWUbkzzsbcVac2qwfssoUJAN1Xhrg6bNk8J7Nzm5H7kxEbn2Nh2ZoGg";
    auto const secret = "cbf4b9f70470856bb4f40f80b87edb90865997ffee6df315ab166d713af433a5"_base16;
    KD_REQUIRE_ENCRYPT(secret, "TestingOneTwoThree", version, compression, expected);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  encrypt private  vector 1  expected", "[encrypted  encrypt private]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const expected = "6PRNFFkZc2NZ6dJqFfhRoFNMR9Lnyj7dYGrzdgXXVMXcxoKTePPX1dWByq";
    auto const secret = "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae"_base16;
    KD_REQUIRE_ENCRYPT(secret, "Satoshi", version, compression, expected);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#compression-no-ec-multiply
TEST_CASE("encrypted  encrypt private  vector 2 compressed  expected", "[encrypted  encrypt private]") {
    auto compression = true;
    uint8_t const version = 0x00;
    auto const expected = "6PYNKZ1EAgYgmQfmNVamxyXVWHzK5s6DGhwP4J5o44cvXdoY7sRzhtpUeo";
    auto const secret = "cbf4b9f70470856bb4f40f80b87edb90865997ffee6df315ab166d713af433a5"_base16;
    KD_REQUIRE_ENCRYPT(secret, "TestingOneTwoThree", version, compression, expected);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#compression-no-ec-multiply
TEST_CASE("encrypted  encrypt private  vector 3 compressed  expected", "[encrypted  encrypt private]") {
    auto compression = true;
    uint8_t const version = 0x00;
    auto const expected = "6PYLtMnXvfG3oJde97zRyLYFZCYizPU5T3LwgdYJz1fRhh16bU7u6PPmY7";
    auto const secret = "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae"_base16;
    KD_REQUIRE_ENCRYPT(secret, "Satoshi", version, compression, expected);
}

// #3 from: github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  encrypt private  vector unicode  expected", "[encrypted  encrypt private]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const encoded_password = "cf92cc8100f0909080f09f92a9"_base16;
    std::string passphrase(encoded_password.begin(), encoded_password.end());
    auto const expected = "6PRW5o9FLp4gJDDVqJQKJFTpMvdsSGJxMYHtHaQBF3ooa8mwD69bapcDQn";
    auto const secret = "64eeab5f9be2a01a8365a579511eb3373c87c40da6d2a25f05bda68fe077b66e"_base16;
    KD_REQUIRE_ENCRYPT(secret, passphrase, version, compression, expected);
}

// End Test Suite

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  decrypt1

// TODO(legacy): create compressed+multiplied and altchain/testnet vector(s).

#define KD_REQUIRE_DECRYPT_SECRET(key, passphrase) \
    ec_secret out_secret;                          \
    uint8_t out_version = 42;                      \
    bool out_is_compressed = true;                 \
    REQUIRE(decrypt(out_secret, out_version, out_is_compressed, key, passphrase))

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 0  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PRVWUbkzzsbcVac2qwfssoUJAN1Xhrg6bNk8J7Nzm5H7kxEbn2Nh2ZoGg");
    KD_REQUIRE_DECRYPT_SECRET(key, "TestingOneTwoThree");
    REQUIRE(encode_base16(out_secret) == "cbf4b9f70470856bb4f40f80b87edb90865997ffee6df315ab166d713af433a5");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 1  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PRNFFkZc2NZ6dJqFfhRoFNMR9Lnyj7dYGrzdgXXVMXcxoKTePPX1dWByq");
    KD_REQUIRE_DECRYPT_SECRET(key, "Satoshi");
    REQUIRE(encode_base16(out_secret) == "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 2 compressed  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PYNKZ1EAgYgmQfmNVamxyXVWHzK5s6DGhwP4J5o44cvXdoY7sRzhtpUeo");
    KD_REQUIRE_DECRYPT_SECRET(key, "TestingOneTwoThree");
    REQUIRE(encode_base16(out_secret) == "cbf4b9f70470856bb4f40f80b87edb90865997ffee6df315ab166d713af433a5");
    REQUIRE(out_version == expected_version);
    REQUIRE(out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 3 compressed  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PYLtMnXvfG3oJde97zRyLYFZCYizPU5T3LwgdYJz1fRhh16bU7u6PPmY7");
    KD_REQUIRE_DECRYPT_SECRET(key, "Satoshi");
    REQUIRE(encode_base16(out_secret) == "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae");
    REQUIRE(out_version == expected_version);
    REQUIRE(out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 4 multiplied  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PfQu77ygVyJLZjfvMLyhLMQbYnu5uguoJJ4kMCLqWwPEdfpwANVS76gTX");
    KD_REQUIRE_DECRYPT_SECRET(key, "TestingOneTwoThree");
    REQUIRE(encode_base16(out_secret) == "a43a940577f4e97f5c4d39eb14ff083a98187c64ea7c99ef7ce460833959a519");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#no-compression-no-ec-multiply
TEST_CASE("encrypted  decrypt private  vector 5 multiplied  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PfLGnQs6VZnrNpmVKfjotbnQuaJK4KZoPFrAjx1JMJUa1Ft8gnf5WxfKd");
    KD_REQUIRE_DECRYPT_SECRET(key, "Satoshi");
    REQUIRE(encode_base16(out_secret) == "c2c8036df268f498099350718c4a3ef3984d2be84618c2650f5171dcc5eb660a");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#ec-multiply-no-compression-lotsequence-numbers
TEST_CASE("encrypted  decrypt private  vector 6 multiplied lot  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PgNBNNzDkKdhkT6uJntUXwwzQV8Rr2tZcbkDcuC9DZRsS6AtHts4Ypo1j");
    KD_REQUIRE_DECRYPT_SECRET(key, "MOLON LABE");
    REQUIRE(encode_base16(out_secret) == "44ea95afbf138356a05ea32110dfd627232d0f2991ad221187be356f19fa8190");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#ec-multiply-no-compression-lotsequence-numbers
TEST_CASE("encrypted  decrypt private  vector 7 multiplied lot  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PgGWtx25kUg8QWvwuJAgorN6k9FbE25rv5dMRwu5SKMnfpfVe5mar2ngH");
    KD_REQUIRE_DECRYPT_SECRET(key, "ΜΟΛΩΝ ΛΑΒΕ");
    REQUIRE(encode_base16(out_secret) == "ca2759aa4adb0f96c414f36abeb8db59342985be9fa50faac228c8e7d90e3006");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  decrypt private  vector 8 multiplied  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PfPAw5HErFdzMyBvGMwSfSWjKmzgm3jDg7RxQyVCSSBJFZLAZ6hVupmpn");
    KD_REQUIRE_DECRYPT_SECRET(key, "kth test");
    REQUIRE(encode_base16(out_secret) == "fb4bfb0bfe151d524b0b11983b9f826d6a0bc7f7bdc480864a1b557ff0c59eb4");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  decrypt private  vector 9 multiplied  expected", "[encrypted  decrypt1]") {
    uint8_t const expected_version = 0x00;
    auto const key = base58_literal("6PfU2yS6DUHjgH8wmsJRT1rHWXRofmDV5UJ3dypocew56BDcw5TQJXFYfm");
//TODO(kth): replace the key
    KD_REQUIRE_DECRYPT_SECRET(key, "Libbitcoin BIP38 Test Vector");
    REQUIRE(encode_base16(out_secret) == "97c745cc980e5a070e12d0bff3f539b70748aadb406045fc1b42d4ded559a564");
    REQUIRE(out_version == expected_version);
    REQUIRE( ! out_is_compressed);
}

// End Test Suite

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  decrypt public

// TODO(legacy): create compressed and altchain/testnet vector(s).

#define KD_REQUIRE_DECRYPT_POINT(key, passphrase, version)                              \
    ec_compressed out_point;                                                            \
    uint8_t out_version = 42;                                                           \
    bool out_is_compressed = true;                                                      \
    REQUIRE(decrypt(out_point, out_version, out_is_compressed, key, passphrase));
    REQUIRE(out_version == version);
    REQUIRE( ! out_is_compressed);
    auto const derived_address = payment_address({out_point, out_is_compressed}, out_version).encoded()

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#ec-multiply-no-compression-lotsequence-numbers
TEST_CASE("encrypted  decrypt public  vector 6 lot  expected", "[encrypted  decrypt public]") {
    uint8_t const version = 0x00;
    auto const key = base58_literal("cfrm38V8aXBn7JWA1ESmFMUn6erxeBGZGAxJPY4e36S9QWkzZKtaVqLNMgnifETYw7BPwWC9aPD");
    KD_REQUIRE_DECRYPT_POINT(key, "MOLON LABE", version);
    REQUIRE(encode_base16(out_point) == "03e20f3b812f630d0374eefe776e983549d5cdde87780be45cb9def7ee339dfed4");
    REQUIRE(derived_address == "1Jscj8ALrYu2y9TD8NrpvDBugPedmbj4Yh");
}

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki#ec-multiply-no-compression-lotsequence-numbers
TEST_CASE("encrypted  decrypt public  vector 7 lot  expected", "[encrypted  decrypt public]") {
    uint8_t const version = 0x00;
    auto const key = base58_literal("cfrm38V8G4qq2ywYEFfWLD5Cc6msj9UwsG2Mj4Z6QdGJAFQpdatZLavkgRd1i4iBMdRngDqDs51");
    KD_REQUIRE_DECRYPT_POINT(key, "ΜΟΛΩΝ ΛΑΒΕ", version);
    REQUIRE(encode_base16(out_point) == "0215fb4e4e62fcec936920dbda69e83facfe2cc5e152fafcf474c8fa0dcf5023f3");
    REQUIRE(derived_address == "1Lurmih3KruL4xDB5FmHof38yawNtP9oGf");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  decrypt public  vector 8  expected", "[encrypted  decrypt public]") {
    uint8_t const version = 0x00;
    auto const key = base58_literal("cfrm38V5Nm1mn7GxPBAGTXawqXRwE1EbR19GqsvJ9JmF5VKLqi8nETmULpELkQvExCGkTNCH2An");
    KD_REQUIRE_DECRYPT_POINT(key, "kth test", version);
    REQUIRE(encode_base16(out_point) == "02c13b65302bbbed4f7ad67bc68e928b58e7748d84091a2d42680dc52e7916079e");
    REQUIRE(derived_address == "1NQgLnFz1ZzF6KkCJ4SM3xz3Jy1q2hEEax");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  decrypt public  vector 9  expected", "[encrypted  decrypt public]") {
    uint8_t const version = 0x00;
    auto const key = base58_literal("cfrm38V5ec4E5RKwBu46Jf5zfaE54nuB1NWHpHSpgX4GQqfzx7fvqm43mBHvr89pPgykDHts9VC");

//TODO(kth): replace the key
    KD_REQUIRE_DECRYPT_POINT(key, "Libbitcoin BIP38 Test Vector", version);
    REQUIRE(encode_base16(out_point) == "02c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52");
    REQUIRE(derived_address == "1NjjvGqXDrx1DvrhjYJxzrpyopk1ygHTSJ");
}

// End Test Suite

#endif  // WITH_ICU

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  create key pair

// TODO(legacy): create compressed vector(s).

#define KD_REQUIRE_CREATE_KEY_PAIR(token, seed, version, compressed) \
    ec_compressed out_point;                                         \
    encrypted_private out_private;                                   \
    REQUIRE(create_key_pair(out_private, out_point, token, seed, version, compressed))

TEST_CASE("encrypted  create key pair  bad checksum  false", "[encrypted  create key pair]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const seed = "d36d8e703d8bd5445044178f69087657fba73d9f3ff211f7"_base16;
    auto const token = base58_literal("passphraseo59BauW85etaRsKpbbTrEa5RRYw6bq5K9yrDf4r4N5fcirPdtDKmfJw9oYNoGN");
    ec_compressed out_point;
    encrypted_private out_private;
    REQUIRE( ! create_key_pair(out_private, out_point, token, seed, version, compression));
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair  vector 8  expected", "[encrypted  create key pair]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const seed = "d36d8e703d8bd5445044178f69087657fba73d9f3ff211f7"_base16;
    auto const token = base58_literal("passphraseo59BauW85etaRsKpbbTrEa5RRYw6bq5K9yrDf4r4N5fcirPdtDKmfJw9oYNoGM");
    KD_REQUIRE_CREATE_KEY_PAIR(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PfPAw5HErFdzMyBvGMwSfSWjKmzgm3jDg7RxQyVCSSBJFZLAZ6hVupmpn");

    ec_uncompressed decompressed;
    REQUIRE(decompress(decompressed, out_point));
    REQUIRE(encode_base16(decompressed) == "04c13b65302bbbed4f7ad67bc68e928b58e7748d84091a2d42680dc52e7916079e103bd025079e984fb4439177224e48a2d9da5768d9b886d89d22c714169723a6");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair  vector 9  expected", "[encrypted  create key pair]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PfU2yS6DUHjgH8wmsJRT1rHWXRofmDV5UJ3dypocew56BDcw5TQJXFYfm");

    ec_uncompressed decompressed;
    REQUIRE(decompress(decompressed, out_point));
    REQUIRE(encode_base16(decompressed) == "04c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52858af96a727252a99c54e871ff7bcf9b53cb74e4da1e15d9e83625e3c91222c0");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair  vector 9 compressed  expected", "[encrypted  create key pair]") {
    auto compression = true;
    uint8_t const version = 0x00;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PnQ4ihgH1pxeUWa1SDPZ4xToaTdLtjebd8Qw6KJf8xDCW67ssaAqWuJkw");
    REQUIRE(encode_base16(out_point) == "02c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52");
}

// altchain vectors are based on preliminary bidirectional mapping proposal.
TEST_CASE("encrypted  create key pair  vector 9 compressed testnet  expected", "[encrypted  create key pair]") {
    auto compression = true;
    uint8_t const version = 111;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "8FELCpEDogaLG3WkLhSVpKKravcNDZ7HAQ7jwHApt1Rn4BHqaLAfo9nrRD");
    REQUIRE(encode_base16(out_point) == "02c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52");
}

// End Test Suite

// ----------------------------------------------------------------------------

// Start Test Suite: encrypted  create key pair with confirmation

// TODO(legacy): create compressed vector(s).

#define KD_REQUIRE_CREATE_KEY_PAIR_CONFIRMATION(token, seed, version, compressed) \
    ec_compressed out_point;                                                      \
    encrypted_public out_public;                                                  \
    encrypted_private out_private;                                                \
    REQUIRE(create_key_pair(out_private, out_public, out_point, token, seed, version, compressed))

TEST_CASE("encrypted  create key pair with confirmation  bad checksum  false", "[encrypted  create key pair with confirmation]") {
    auto const seed = "d36d8e703d8bd5445044178f69087657fba73d9f3ff211f7"_base16;
    auto const token = base58_literal("passphraseo59BauW85etaRsKpbbTrEa5RRYw6bq5K9yrDf4r4N5fcirPdtDKmfJw9oYNoGN");
    ec_compressed out_point;
    encrypted_public out_public;
    encrypted_private out_private;
    REQUIRE( ! create_key_pair(out_private, out_public, out_point, token, seed, 0, false));
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair with confirmation  vector 8  expected", "[encrypted  create key pair with confirmation]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const seed = "d36d8e703d8bd5445044178f69087657fba73d9f3ff211f7"_base16;
    auto const token = base58_literal("passphraseo59BauW85etaRsKpbbTrEa5RRYw6bq5K9yrDf4r4N5fcirPdtDKmfJw9oYNoGM");
    KD_REQUIRE_CREATE_KEY_PAIR_CONFIRMATION(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PfPAw5HErFdzMyBvGMwSfSWjKmzgm3jDg7RxQyVCSSBJFZLAZ6hVupmpn");
    REQUIRE(encode_base58(out_public) == "cfrm38V5Nm1mn7GxPBAGTXawqXRwE1EbR19GqsvJ9JmF5VKLqi8nETmULpELkQvExCGkTNCH2An");

    ec_uncompressed decompressed;
    REQUIRE(decompress(decompressed, out_point));
    REQUIRE(encode_base16(decompressed) == "04c13b65302bbbed4f7ad67bc68e928b58e7748d84091a2d42680dc52e7916079e103bd025079e984fb4439177224e48a2d9da5768d9b886d89d22c714169723a6");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair with confirmation  vector 9  expected", "[encrypted  create key pair with confirmation]") {
    auto compression = false;
    uint8_t const version = 0x00;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR_CONFIRMATION(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PfU2yS6DUHjgH8wmsJRT1rHWXRofmDV5UJ3dypocew56BDcw5TQJXFYfm");
    REQUIRE(encode_base58(out_public) == "cfrm38V5ec4E5RKwBu46Jf5zfaE54nuB1NWHpHSpgX4GQqfzx7fvqm43mBHvr89pPgykDHts9VC");

    ec_uncompressed decompressed;
    REQUIRE(decompress(decompressed, out_point));
    REQUIRE(encode_base16(decompressed) == "04c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52858af96a727252a99c54e871ff7bcf9b53cb74e4da1e15d9e83625e3c91222c0");
}

// generated and verified using bit2factor.com, no lot/sequence
TEST_CASE("encrypted  create key pair with confirmation  vector 9 compressed  expected", "[encrypted  create key pair with confirmation]") {
    auto compression = true;
    uint8_t const version = 0x00;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR_CONFIRMATION(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "6PnQ4ihgH1pxeUWa1SDPZ4xToaTdLtjebd8Qw6KJf8xDCW67ssaAqWuJkw");
    REQUIRE(encode_base58(out_public) == "cfrm38VUEdzHWKfUjdNjV22wyFNGgtRHYhXdBFT7fWw7cCJbCobryAYUThq4BbTPP15g4SeBsug");
    REQUIRE(encode_base16(out_point) == "02c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52");
}

// altchain vectors are based on preliminary bidirectional mapping proposal.
TEST_CASE("encrypted  create key pair with confirmation  vector 9 compressed testnet  expected", "[encrypted  create key pair with confirmation]") {
    auto compression = true;
    uint8_t const version = 111;
    auto const seed = "bbeac8b9bb39381520b6873553544b387bcaa19112602230"_base16;
    auto const token = base58_literal("passphraseouGLY8yjTZQ5Q2bTo8rtKfdbHz4tme7QuPheRgES8KnT6pX5yxFauYhv3SVPDD");
    KD_REQUIRE_CREATE_KEY_PAIR_CONFIRMATION(token, seed, version, compression);
    REQUIRE(encode_base58(out_private) == "8FELCpEDogaLG3WkLhSVpKKravcNDZ7HAQ7jwHApt1Rn4BHqaLAfo9nrRD");
    REQUIRE(encode_base58(out_public) == "cfrm2zc77zW4FRDALEVBoKmmT79Q7KshtvLZoN62JADnXGPEcPosMx8sM8Ry4ieGW3FXUEoBwk2");
    REQUIRE(encode_base16(out_point) == "02c3b28a224e38af4219cd782653250d2e4b67ed85ac342201f8f05ff909efdc52");
}

// End Test Suite

// ----------------------------------------------------------------------------

#ifdef WITH_ICU

// Start Test Suite: encrypted  round trips

TEST_CASE("encrypted  encrypt  compressed testnet  matches secret version and compression", "[encrypted  round trips]") {
    auto const secret = "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae"_base16;
    auto const passphrase = "passphrase";

    // Encrypt the secret as a private key.
    encrypted_private out_private_key;
    uint8_t const version = 111;
    auto const is_compressed = true;
    auto const seed = "baadf00dbaadf00dbaadf00dbaadf00dbaadf00dbaadf00d"_base16;
    REQUIRE(encrypt(out_private_key, secret, passphrase, version, is_compressed));

    // Decrypt the secret from the private key.
    auto const& private_key = out_private_key;
    ec_secret out_secret;
    uint8_t out_version = 42;
    bool out_is_compressed = false;
    REQUIRE(decrypt(out_secret, out_version, out_is_compressed, private_key, passphrase));
    REQUIRE(encode_base16(out_secret) == encode_base16(secret));
    REQUIRE(out_is_compressed == is_compressed);
    REQUIRE(out_version == version);
}

TEST_CASE("encrypted  create token entropy  private uncompressed testnet  decrypts with matching version and compression", "[encrypted  round trips]") {
    // Create the token.
    encrypted_token out_token;
    auto const passphrase = "passphrase";
    auto const entropy = "baadf00dbaadf00d"_base16;
    REQUIRE(create_token(out_token, passphrase, entropy));
    REQUIRE(encode_base58(out_token) == "passphraseqVHzjNrYRo5G6sfRB4YdSaQ2m8URnkBYS1UT6JBju5G5o45YRZKLDpK6J3PEGq");

    // Create the private key.
    auto const& token = out_token;
    ec_compressed out_point;
    encrypted_private out_private_key;
    uint8_t const version = 111;
    auto const is_compressed = false;
    auto const seed = "baadf00dbaadf00dbaadf00dbaadf00dbaadf00dbaadf00d"_base16;
    REQUIRE(create_key_pair(out_private_key, out_point, token, seed, version, is_compressed));

    // Extract the secret from the private key.
    auto const& private_key = out_private_key;
    ec_secret out_secret;
    uint8_t out_version = 42;
    bool out_is_compressed = true;
    REQUIRE(decrypt(out_secret, out_version, out_is_compressed, private_key, passphrase));
    REQUIRE(out_is_compressed == is_compressed);
    REQUIRE(out_version == version);

    // Validate the point derived from key creation against the secret-derived point.
    // The out point is always compressed, since the user can always decompress it.
    ec_compressed compressed;
    REQUIRE(secret_to_public(compressed, out_secret));
    REQUIRE(encode_base16(out_point) == encode_base16(compressed));
}

TEST_CASE("encrypted  create token lot  private and public compressed testnet  decrypts with matching version and compression", "[encrypted  round trips]") {
    // Create the token.
    encrypted_token out_token;
    auto const passphrase = "passphrase";
    auto const salt = "baadf00d"_base16;
    REQUIRE(create_token(out_token, passphrase, salt, 42, 24));
    REQUIRE(encode_base58(out_token) == "passphrasecpXbDpHuo8FGWnwMTnTFiHSDnqyARArE2YSFQzMHtCZvM2oWg2K3Ua2crKyc11");

    // Create the public/private key pair.
    auto const& token = out_token;
    ec_compressed out_point;
    encrypted_private out_private_key;
    encrypted_public out_public_key;
    uint8_t const version = 111;
    auto const is_compressed = true;
    auto const seed = "baadf00dbaadf00dbaadf00dbaadf00dbaadf00dbaadf00d"_base16;
    REQUIRE(create_key_pair(out_private_key, out_public_key, out_point, token, seed, version, is_compressed));

    // Extract the secret from the private key.
    auto const& private_key = out_private_key;
    ec_secret out_secret1;
    uint8_t out_version1 = 42;
    bool out_is_compressed1 = false;
    ec_compressed compressed;
    REQUIRE(decrypt(out_secret1, out_version1, out_is_compressed1, private_key, passphrase));
    REQUIRE(out_is_compressed1 == is_compressed);
    REQUIRE(out_version1 == version);
    REQUIRE(secret_to_public(compressed, out_secret1));
    REQUIRE(encode_base16(out_point) == encode_base16(compressed));

    // Extract the point from the public key.
    auto const& public_key = out_public_key;
    ec_compressed out_point2;
    uint8_t out_version2 = 42;
    bool out_is_compressed2 = false;
    REQUIRE(decrypt(out_point2, out_version2, out_is_compressed2, public_key, passphrase));
    REQUIRE(out_is_compressed2 == is_compressed);
    REQUIRE(out_version2 == version);
    REQUIRE(encode_base16(out_point2) == encode_base16(compressed));
}

// End Test Suite

#endif

// ----------------------------------------------------------------------------

// These are not actual tests, just for emitting the version maps.

// Start Test Suite: encrypted  altchain versions
//
//static std::string hex(uint8_t number)
//{
//    return encode_base16(data_chunk{ number });
//}
//
// TEST_CASE("encrypted  create key pair  all versions  print private and public encrypted keys", "[None]")
//{
//    encrypted_private out_private_key;
//    auto const secret = "09c2686880095b1a4c249ee3ac4eea8a014f11e6f986d0b5025ac1f39afbd9ae"_base16;
//
//    auto const compressed = true;
//    for (size_t version = 0x00; version <= 0xFF; ++version)
//    {
//        auto const byte = static_cast<uint8_t>(version);
//       REQUIRE(encrypt(out_private_key, secret, "passphrase", byte, compressed));
//
//        auto key = encode_base58(out_private_key);
//        key.resize(2);
//        BOOST_TEST_MESSAGE("0x" + hex(byte) + " " + key);
//    }
//}
//
// TEST_CASE("encrypted  create key pair  all multiplied versions  print private and public encrypted keys", "[None]")
//{
//    encrypted_token out_token;
//    create_token(out_token, "passphrase", "baadf00dbaadf00d"_base16);
//
//    auto const& token = out_token;
//    ec_compressed unused;
//    encrypted_private out_private_key;
//    encrypted_public out_public_key;
//    auto const seed = "baadf00dbaadf00dbaadf00dbaadf00dbaadf00dbaadf00d"_base16;
//
//    auto const compressed = true;
//    for (size_t version = 0x00; version <= 0xFF; ++version)
//    {
//        auto const byte = static_cast<uint8_t>(version);
//       REQUIRE(create_key_pair(out_private_key, out_public_key, unused, token, seed, byte, compressed));
//
//        auto private_key = encode_base58(out_private_key);
//        auto public_key = encode_base58(out_public_key);
//        private_key.resize(2);
//        public_key.resize(7);
//        BOOST_TEST_MESSAGE("0x" + hex(byte) + " " + private_key + " " + public_key);
//    }
//}
//
// End Test Suite

// ----------------------------------------------------------------------------

// End Test Suite
