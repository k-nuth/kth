// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is named .cpp solely so it can use Catch2 (which is C++).
// Everything inside the test bodies is plain C: no namespaces, no
// templates, no <chrono>, no std::*, no auto, no references, no constexpr.
// Only Catch2's TEST_CASE / REQUIRE macros are C++. The point is that
// these tests must exercise the C-API exactly the way a C consumer would.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/primitives.h>
#include <kth/capi/wallet/ek_private.h>
#include <kth/capi/wallet/ek_token.h>
#include <kth/capi/wallet/encrypted_keys.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// BIP38 test vectors — lifted from the domain suite
// (`src/domain/test/wallet/encrypted_keys.cpp`: "create key pair vector 8",
// no lot/sequence, compressed=false, version=0x00). Originally generated
// against bit2factor.com, so any drift in our AES / SCrypt wiring trips
// the round-trip encoded-output check below.
// ---------------------------------------------------------------------------

static char const* const kToken =
    "passphraseo59BauW85etaRsKpbbTrEa5RRYw6bq5K9yrDf4r4N5fcirPdtDKmfJw9oYNoGM";

// 24-byte seed: hex "d36d8e703d8bd5445044178f69087657fba73d9f3ff211f7".
static kth_ek_seed_t const kSeed = {{
    0xd3, 0x6d, 0x8e, 0x70, 0x3d, 0x8b, 0xd5, 0x44,
    0x50, 0x44, 0x17, 0x8f, 0x69, 0x08, 0x76, 0x57,
    0xfb, 0xa7, 0x3d, 0x9f, 0x3f, 0xf2, 0x11, 0xf7,
}};

static uint8_t const kVersion = 0x00u;

// Expected encoded private from vector 8.
static char const* const kExpectedPrivate =
    "6PfPAw5HErFdzMyBvGMwSfSWjKmzgm3jDg7RxQyVCSSBJFZLAZ6hVupmpn";

// ---------------------------------------------------------------------------
// create_key_pair — happy path
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair (vector 8, uncompressed) round-trips",
          "[C-API WalletEncryptedKeys][create_key_pair]") {
    // Build the token through the public ek_token C-API (parsing the
    // base58-check string into the raw 53-byte payload the encryption
    // function expects). This keeps the test honest: it exercises the
    // same input path a real C consumer would use.
    kth_ek_token_mut_t token_handle = kth_wallet_ek_token_construct_from_encoded(kToken);
    REQUIRE(token_handle != NULL);
    kth_encrypted_token_t const token = kth_wallet_ek_token_token(token_handle);
    kth_wallet_ek_token_destruct(token_handle);

    kth_encrypted_private_t out_private = {{ 0 }};
    kth_ec_compressed_t out_point = {{ 0 }};
    kth_bool_t const ok = kth_wallet_encrypted_keys_create_key_pair(
        &out_private, &out_point, &token, &kSeed, kVersion, /*compressed=*/0);
    REQUIRE(ok != 0);

    // Re-wrap the resulting 43-byte payload in an ek_private so we can
    // compare against the expected base58-check string.
    kth_ek_private_mut_t priv = kth_wallet_ek_private_construct_from_value(&out_private);
    REQUIRE(priv != NULL);
    char* encoded = kth_wallet_ek_private_encoded(priv);
    REQUIRE(encoded != NULL);
    REQUIRE(strcmp(encoded, kExpectedPrivate) == 0);
    kth_core_destruct_string(encoded);
    kth_wallet_ek_private_destruct(priv);
}

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair_unsafe matches the safe variant",
          "[C-API WalletEncryptedKeys][create_key_pair]") {
    // Hit the same code path through the raw-pointer entry. Both must
    // agree; otherwise the safe/unsafe pair has drifted.
    kth_ek_token_mut_t token_handle = kth_wallet_ek_token_construct_from_encoded(kToken);
    REQUIRE(token_handle != NULL);
    kth_encrypted_token_t const token = kth_wallet_ek_token_token(token_handle);
    kth_wallet_ek_token_destruct(token_handle);

    kth_encrypted_private_t out_private_a = {{ 0 }};
    kth_ec_compressed_t   out_point_a    = {{ 0 }};
    REQUIRE(kth_wallet_encrypted_keys_create_key_pair(
        &out_private_a, &out_point_a, &token, &kSeed, kVersion, 0) != 0);

    kth_encrypted_private_t out_private_b = {{ 0 }};
    kth_ec_compressed_t   out_point_b    = {{ 0 }};
    REQUIRE(kth_wallet_encrypted_keys_create_key_pair_unsafe(
        &out_private_b, &out_point_b, token.data, kSeed.data, kVersion, 0) != 0);

    REQUIRE(memcmp(out_private_a.data, out_private_b.data, sizeof(out_private_a.data)) == 0);
    REQUIRE(memcmp(out_point_a.data,   out_point_b.data,   sizeof(out_point_a.data))   == 0);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair(NULL out_private) aborts",
          "[C-API WalletEncryptedKeys][precondition]") {
    kth_encrypted_token_t const token = {{ 0 }};
    kth_ec_compressed_t out_point = {{ 0 }};
    KTH_EXPECT_ABORT(kth_wallet_encrypted_keys_create_key_pair(
        NULL, &out_point, &token, &kSeed, 0, 0));
}

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair(NULL out_point) aborts",
          "[C-API WalletEncryptedKeys][precondition]") {
    kth_encrypted_token_t const token = {{ 0 }};
    kth_encrypted_private_t out_private = {{ 0 }};
    KTH_EXPECT_ABORT(kth_wallet_encrypted_keys_create_key_pair(
        &out_private, NULL, &token, &kSeed, 0, 0));
}

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair(NULL token) aborts",
          "[C-API WalletEncryptedKeys][precondition]") {
    kth_encrypted_private_t out_private = {{ 0 }};
    kth_ec_compressed_t out_point = {{ 0 }};
    KTH_EXPECT_ABORT(kth_wallet_encrypted_keys_create_key_pair(
        &out_private, &out_point, NULL, &kSeed, 0, 0));
}

TEST_CASE("C-API wallet::encrypted_keys - create_key_pair(NULL seed) aborts",
          "[C-API WalletEncryptedKeys][precondition]") {
    kth_encrypted_token_t const token = {{ 0 }};
    kth_encrypted_private_t out_private = {{ 0 }};
    kth_ec_compressed_t out_point = {{ 0 }};
    KTH_EXPECT_ABORT(kth_wallet_encrypted_keys_create_key_pair(
        &out_private, &out_point, &token, NULL, 0, 0));
}
