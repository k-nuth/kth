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

#include <kth/capi/binary.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/wallet/stealth_address.h>
#include <kth/capi/wallet/stealth_receiver.h>
#include <kth/capi/wallet/stealth_sender.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — shared with the stealth_receiver tests so the
// round-trip scenario below can exercise both sides with a single
// derivation.
// ---------------------------------------------------------------------------

static kth_hash_t const kScanPrivate = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
}};

static kth_hash_t const kSpendPrivate = {{
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
    0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
    0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
}};

// Arbitrary valid secp256k1 scalar, distinct from the scan/spend keys.
static kth_hash_t const kEphemeralPrivate = {{
    0xf9, 0x1e, 0x67, 0x31, 0x03, 0x86, 0x3b, 0xbe,
    0xb0, 0xef, 0x18, 0x52, 0xcd, 0x8e, 0xad, 0xe6,
    0xb7, 0x3e, 0xa5, 0x5a, 0xfc, 0x9b, 0x18, 0x73,
    0xbe, 0x62, 0xbf, 0x62, 0x8e, 0xac, 0x07, 0x2a,
}};

static uint8_t const kMainnetP2KH = 0x00u;

// Seed bytes passed to the sender's factory. BIP63 salts an internal
// RNG for the ephemeral key. Using a fixed seed keeps the test
// deterministic; the exact value doesn't matter for the invariants
// we check (handle non-null, payment_address accessible, script
// present).
static uint8_t const kSeedBytes[] = {
    0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xfa, 0xce,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
};

// Small helper to build a stealth_receiver fixture, so each test case
// can start from a fresh valid stealth_address without repeating the
// scan/spend private setup.
static kth_stealth_receiver_mut_t make_receiver(kth_binary_const_t filter) {
    kth_stealth_receiver_mut_t r = NULL;
    kth_wallet_stealth_receiver_from_secrets(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH, &r);
    return r;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - from_ephemeral happy path",
          "[C-API WalletStealthSender][lifecycle]") {
    // The sender needs a stealth_address to send to. Generate one via
    // stealth_receiver so the fixture is self-contained.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = make_receiver(filter);
    REQUIRE(r != NULL);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);
    REQUIRE(addr != NULL);

    kth_stealth_sender_mut_t s = NULL;
    REQUIRE(kth_wallet_stealth_sender_from_ephemeral(
        &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &s) == kth_ec_success);
    REQUIRE(s != NULL);

    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - destruct(NULL) is a no-op",
          "[C-API WalletStealthSender][lifecycle]") {
    kth_wallet_stealth_sender_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Getters — stealth_script + payment_address
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - stealth_script returns a non-null handle",
          "[C-API WalletStealthSender][accessor]") {
    // The sender's stealth_script is the OP_RETURN-prefixed output
    // that carries the ephemeral public key. It must be non-null
    // after successful construction.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = make_receiver(filter);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s = NULL;
    REQUIRE(kth_wallet_stealth_sender_from_ephemeral(
        &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &s) == kth_ec_success);
    REQUIRE(s != NULL);

    kth_script_const_t script = kth_wallet_stealth_sender_stealth_script(s);
    REQUIRE(script != NULL);

    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - payment_address returns a non-null handle",
          "[C-API WalletStealthSender][accessor]") {
    // The sender's derived payment_address is what the sender would
    // actually send funds to.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = make_receiver(filter);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s = NULL;
    REQUIRE(kth_wallet_stealth_sender_from_ephemeral(
        &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &s) == kth_ec_success);
    REQUIRE(s != NULL);

    kth_payment_address_const_t pay = kth_wallet_stealth_sender_payment_address(s);
    REQUIRE(pay != NULL);

    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - copy yields a distinct handle",
          "[C-API WalletStealthSender][value]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = make_receiver(filter);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t a = NULL;
    REQUIRE(kth_wallet_stealth_sender_from_ephemeral(
        &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &a) == kth_ec_success);
    REQUIRE(a != NULL);

    kth_stealth_sender_mut_t b = kth_wallet_stealth_sender_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(b != a);

    kth_wallet_stealth_sender_destruct(b);
    kth_wallet_stealth_sender_destruct(a);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - from_ephemeral(NULL address) aborts",
          "[C-API WalletStealthSender][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_sender_mut_t s = NULL;
    KTH_EXPECT_ABORT(kth_wallet_stealth_sender_from_ephemeral(
        &kEphemeralPrivate, NULL, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &s));
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - from_ephemeral(NULL ephemeral_private) aborts",
          "[C-API WalletStealthSender][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = make_receiver(filter);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s = NULL;
    KTH_EXPECT_ABORT(kth_wallet_stealth_sender_from_ephemeral(
        NULL, addr, kSeedBytes, sizeof(kSeedBytes),
        filter, kMainnetP2KH, &s));

    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}
