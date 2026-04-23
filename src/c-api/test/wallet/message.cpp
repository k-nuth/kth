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

#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/ec_private.h>
#include <kth/capi/wallet/message.h>
#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/wallet/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — a fully-deterministic 32-byte secret keeps the signature
// stable across runs, so we don't need to re-sign for every assertion.
// ---------------------------------------------------------------------------

static kth_hash_t const kSecret = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static uint8_t const kMessage[] = "hello Knuth";
static kth_size_t const kMessageLen = sizeof(kMessage) - 1; // drop '\0'

// ---------------------------------------------------------------------------
// hash_message — deterministic, well-defined for empty input
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::message - hash_message yields a stable 32-byte digest",
          "[C-API WalletMessage][hash]") {
    kth_hash_t a = kth_wallet_message_hash_message(kMessage, kMessageLen);
    kth_hash_t b = kth_wallet_message_hash_message(kMessage, kMessageLen);
    // Same input → same digest (deterministic BIP137 preamble + SHA256²).
    REQUIRE(kth_hash_equal(a, b) != 0);
    REQUIRE(kth_hash_is_null(a) == 0);  // non-null digest
}

TEST_CASE("C-API wallet::message - hash_message accepts empty input",
          "[C-API WalletMessage][hash]") {
    kth_hash_t h = kth_wallet_message_hash_message(NULL, 0);
    // Empty message still hashes cleanly (the preamble alone seeds the digest).
    REQUIRE(kth_hash_is_null(h) == 0);
}

// ---------------------------------------------------------------------------
// Recovery-id ↔ magic byte round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::message - recovery_id ↔ magic round-trip",
          "[C-API WalletMessage][recovery]") {
    // BIP137 recovery ids range 0..3 for each (compressed, uncompressed)
    // flavour. Round-trip every value through magic-byte encoding to
    // confirm the two helpers are proper inverses.
    for (uint8_t rid = 0; rid <= 3; ++rid) {
        for (int compressed = 0; compressed <= 1; ++compressed) {
            uint8_t magic = 0;
            REQUIRE(kth_wallet_message_recovery_id_to_magic(
                &magic, rid, (kth_bool_t)compressed) != 0);

            uint8_t back_rid = 0xff;
            kth_bool_t back_compressed = compressed ? 0 : 1;
            REQUIRE(kth_wallet_message_magic_to_recovery_id(
                &back_rid, &back_compressed, magic) != 0);

            REQUIRE(back_rid == rid);
            REQUIRE((back_compressed != 0) == (compressed != 0));
        }
    }
}

TEST_CASE("C-API wallet::message - recovery_id_to_magic rejects out-of-range ids",
          "[C-API WalletMessage][recovery]") {
    uint8_t magic = 0xaa;
    // BIP137 only defines 0..3; a larger id must fail and leave the
    // output untouched.
    REQUIRE(kth_wallet_message_recovery_id_to_magic(&magic, 4, 1) == 0);
    REQUIRE(magic == 0xaa);
}

// ---------------------------------------------------------------------------
// Sign / verify round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::message - sign(ec_private) + verify round-trip",
          "[C-API WalletMessage][sign][verify]") {
    // Build a private key from the fixture secret, derive its payment
    // address, sign a message under the key, and verify the signature
    // resolves back to the same address.
    kth_ec_private_mut_t secret = kth_wallet_ec_private_construct_from_secret_version_compress(
        &kSecret, /*version=*/0x8000u, /*compress=*/1);
    REQUIRE(secret != NULL);

    kth_payment_address_mut_t address = kth_wallet_ec_private_to_payment_address(secret);
    REQUIRE(address != NULL);

    kth_message_signature_t sig;
    memset(&sig, 0, sizeof(sig));
    REQUIRE(kth_wallet_message_sign_message_ec_private(
        &sig, kMessage, kMessageLen, secret) != 0);

    // The signature's magic byte encodes (recovery_id, compressed).
    // Compressed keys use magic 31..34 — assert we landed there so a
    // future regression that picks the wrong magic would surface.
    REQUIRE(sig.data[0] >= 31);
    REQUIRE(sig.data[0] <= 34);

    // verify must succeed against the signer's address.
    REQUIRE(kth_wallet_message_verify_message(
        kMessage, kMessageLen, address, &sig) != 0);

    kth_wallet_payment_address_destruct(address);
    kth_wallet_ec_private_destruct(secret);
}

TEST_CASE("C-API wallet::message - verify rejects a tampered message",
          "[C-API WalletMessage][verify]") {
    kth_ec_private_mut_t secret = kth_wallet_ec_private_construct_from_secret_version_compress(
        &kSecret, 0x8000u, 1);
    kth_payment_address_mut_t address = kth_wallet_ec_private_to_payment_address(secret);

    kth_message_signature_t sig;
    memset(&sig, 0, sizeof(sig));
    REQUIRE(kth_wallet_message_sign_message_ec_private(
        &sig, kMessage, kMessageLen, secret) != 0);

    // Flip one byte of the message payload; verify must report failure.
    uint8_t tampered[sizeof(kMessage)];
    memcpy(tampered, kMessage, sizeof(kMessage));
    tampered[0] ^= 0x01;
    REQUIRE(kth_wallet_message_verify_message(
        tampered, kMessageLen, address, &sig) == 0);

    kth_wallet_payment_address_destruct(address);
    kth_wallet_ec_private_destruct(secret);
}

TEST_CASE("C-API wallet::message - sign_hash (raw secret) produces the same result",
          "[C-API WalletMessage][sign]") {
    // Signing with the raw 32-byte secret + `compressed=true` must
    // yield the same signature as signing via the `ec_private` handle
    // (both go through the same underlying ECDSA path).
    kth_ec_private_mut_t secret_handle = kth_wallet_ec_private_construct_from_secret_version_compress(
        &kSecret, 0x8000u, 1);
    kth_message_signature_t via_handle;
    memset(&via_handle, 0, sizeof(via_handle));
    REQUIRE(kth_wallet_message_sign_message_ec_private(
        &via_handle, kMessage, kMessageLen, secret_handle) != 0);

    kth_message_signature_t via_raw;
    memset(&via_raw, 0, sizeof(via_raw));
    REQUIRE(kth_wallet_message_sign_message_hash(
        &via_raw, kMessage, kMessageLen, &kSecret, /*compressed=*/1) != 0);

    REQUIRE(memcmp(via_handle.data, via_raw.data, KTH_MESSAGE_SIGNATURE_SIZE) == 0);

    kth_wallet_ec_private_destruct(secret_handle);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::message - hash null with non-zero size aborts",
          "[C-API WalletMessage][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_message_hash_message(NULL, 1));
}

TEST_CASE("C-API wallet::message - recovery_id_to_magic null out aborts",
          "[C-API WalletMessage][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_message_recovery_id_to_magic(NULL, 0, 1));
}

TEST_CASE("C-API wallet::message - magic_to_recovery_id null outs abort",
          "[C-API WalletMessage][precondition]") {
    kth_bool_t compressed = 0;
    KTH_EXPECT_ABORT(kth_wallet_message_magic_to_recovery_id(NULL, &compressed, 31));
    uint8_t rid = 0;
    KTH_EXPECT_ABORT(kth_wallet_message_magic_to_recovery_id(&rid, NULL, 31));
}
