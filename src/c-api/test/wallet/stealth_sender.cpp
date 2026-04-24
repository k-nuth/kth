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
#include <kth/capi/wallet/elliptic_curve.h>
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

// Seed bytes passed to the non-ephemeral-private ctor. BIP63's sender
// uses this to salt its internal RNG for the ephemeral key. Using a
// fixed seed makes the test deterministic but non-trivially chosen;
// the exact value doesn't matter for the unit-test invariants we
// check (handle non-null, payment_address accessible, script present).
static uint8_t const kSeedBytes[] = {
    0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xfa, 0xce,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
};

// ---------------------------------------------------------------------------
// Lifecycle — via the ephemeral-private ctor (deterministic)
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - construct from ephemeral_private",
          "[C-API WalletStealthSender][lifecycle]") {
    // To build a sender we need a stealth_address. Generate one via
    // stealth_receiver so the fixture is self-contained — avoids
    // pinning a pre-encoded stealth_address string that would drift
    // if the encoding ever changes.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    REQUIRE(r != NULL);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);
    REQUIRE(addr != NULL);

    kth_stealth_sender_mut_t s =
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH);
    REQUIRE(s != NULL);
    REQUIRE(kth_wallet_stealth_sender_valid(s) != 0);

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
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s =
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH);
    REQUIRE(s != NULL);

    kth_script_const_t script = kth_wallet_stealth_sender_stealth_script(s);
    REQUIRE(script != NULL);

    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - payment_address returns a valid handle",
          "[C-API WalletStealthSender][accessor]") {
    // The sender's derived payment_address is what the sender would
    // actually send funds to. Downstream: the receiver can regenerate
    // the same address from just the ephemeral_public via
    // `derive_address` — covered separately.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s =
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH);
    REQUIRE(s != NULL);

    kth_payment_address_const_t pay = kth_wallet_stealth_sender_payment_address(s);
    REQUIRE(pay != NULL);
    REQUIRE(kth_wallet_payment_address_valid(pay) != 0);

    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - copy yields a distinct, valid handle",
          "[C-API WalletStealthSender][value]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t a =
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH);
    REQUIRE(a != NULL);

    kth_stealth_sender_mut_t b = kth_wallet_stealth_sender_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(b != a);
    REQUIRE(kth_wallet_stealth_sender_valid(b) != 0);

    kth_wallet_stealth_sender_destruct(b);
    kth_wallet_stealth_sender_destruct(a);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_sender - construct(NULL address) aborts",
          "[C-API WalletStealthSender][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    KTH_EXPECT_ABORT(
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, NULL, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH));
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - construct(NULL ephemeral_private) aborts",
          "[C-API WalletStealthSender][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    KTH_EXPECT_ABORT(
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            NULL, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH));

    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_sender - valid(NULL) aborts",
          "[C-API WalletStealthSender][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_sender_valid(NULL));
}

// ---------------------------------------------------------------------------
// End-to-end round-trip: sender → receiver.derive_address
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth - sender.payment_address matches receiver.derive_address",
          "[C-API WalletStealthSender][round_trip]") {
    // The BIP63 contract: given the sender's output script containing
    // the ephemeral_public, the receiver can regenerate the same
    // payment_address using only its scan+spend private keys. If this
    // round-trip ever breaks, stealth payments become undiscoverable.
    //
    // We construct both sides from the same fixtures, pull out the
    // sender's payment_address, and derive it again on the receiver
    // via `derive_address(out, ephemeral_public)`. The ephemeral
    // public is recoverable from `stealth_sender::stealth_script()`
    // in principle, but extracting it from the script at the C layer
    // is out of scope for this suite — we use the ephemeral_private's
    // matching public via the secret_to_public helper.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    REQUIRE(r != NULL);
    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);

    kth_stealth_sender_mut_t s =
        kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(
            &kEphemeralPrivate, addr, kSeedBytes, sizeof(kSeedBytes),
            filter, kMainnetP2KH);
    REQUIRE(s != NULL);

    // Pick off the sender-side payment_address — the one a real send
    // transaction would fund. We compare its encoding against the
    // receiver-derived one below.
    kth_payment_address_const_t sender_pay =
        kth_wallet_stealth_sender_payment_address(s);
    REQUIRE(sender_pay != NULL);
    char* sender_encoded = kth_wallet_payment_address_encoded_legacy(sender_pay);
    REQUIRE(sender_encoded != NULL);

    // The receiver's derive_address requires the ephemeral *public*
    // point; compute it from the ephemeral private via the EC helper.
    kth_ec_compressed_t ephemeral_public = {{ 0 }};
    REQUIRE(kth_wallet_secret_to_public(&ephemeral_public,
                                        *(kth_ec_secret_t const*)&kEphemeralPrivate) != 0);

    // Pre-construct an empty payment_address the derive_address
    // function will mutate in place (opaque out-param by ref).
    kth_payment_address_mut_t derived = kth_wallet_payment_address_construct_default();
    REQUIRE(derived != NULL);

    REQUIRE(kth_wallet_stealth_receiver_derive_address(
        r, derived, &ephemeral_public) != 0);

    char* derived_encoded = kth_wallet_payment_address_encoded_legacy(derived);
    REQUIRE(derived_encoded != NULL);

    REQUIRE(strcmp(sender_encoded, derived_encoded) == 0);

    kth_core_destruct_string(derived_encoded);
    kth_core_destruct_string(sender_encoded);
    kth_wallet_payment_address_destruct(derived);
    kth_wallet_stealth_sender_destruct(s);
    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}
