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

#include <kth/capi/binary.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/stealth_address.h>
#include <kth/capi/wallet/stealth_receiver.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Two distinct 32-byte secrets. Arbitrary pseudo-random bytes — the
// BIP63 math only requires that they be valid secp256k1 scalars (below
// the curve order), which any byte pattern that isn't all-zero and
// isn't the max-values neighbourhood satisfies. Chosen so the
// compressed public points are distinct.
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

// Mainnet P2KH: 0x00 (libbitcoin convention); testnet is 0x6f. BIP63
// carries the same version byte through to the derived payment
// address, so we pin mainnet here.
static uint8_t const kMainnetP2KH = 0x00u;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_receiver - construct + valid + destruct",
          "[C-API WalletStealthReceiver][lifecycle]") {
    // Empty binary filter: the receiver accepts every prefix. The
    // stealth_address factory inside the C++ ctor scans the curve for
    // a valid prefix match; an empty filter makes this trivial.
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    REQUIRE(r != NULL);
    REQUIRE(kth_wallet_stealth_receiver_valid(r) != 0);

    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_receiver - destruct(NULL) is a no-op",
          "[C-API WalletStealthReceiver][lifecycle]") {
    kth_wallet_stealth_receiver_destruct(NULL);
}

// ---------------------------------------------------------------------------
// stealth_address accessor
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_receiver - stealth_address returns a valid handle",
          "[C-API WalletStealthReceiver][accessor]") {
    // Accessor returns a borrowed view into `self`. It must be
    // non-null and the returned stealth_address must be in a valid
    // state (the ctor otherwise short-circuits to NULL).
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t r = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    REQUIRE(r != NULL);

    kth_stealth_address_const_t addr = kth_wallet_stealth_receiver_stealth_address(r);
    REQUIRE(addr != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(addr) != 0);

    kth_wallet_stealth_receiver_destruct(r);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_receiver - copy yields a distinct, valid handle",
          "[C-API WalletStealthReceiver][value]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    kth_stealth_receiver_mut_t a = kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, filter, kMainnetP2KH);
    REQUIRE(a != NULL);

    kth_stealth_receiver_mut_t b = kth_wallet_stealth_receiver_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(b != a);
    REQUIRE(kth_wallet_stealth_receiver_valid(b) != 0);

    kth_wallet_stealth_receiver_destruct(b);
    kth_wallet_stealth_receiver_destruct(a);
    kth_core_binary_destruct(filter);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::stealth_receiver - construct(NULL scan_private) aborts",
          "[C-API WalletStealthReceiver][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    KTH_EXPECT_ABORT(kth_wallet_stealth_receiver_construct(
        NULL, &kSpendPrivate, filter, kMainnetP2KH));
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_receiver - construct(NULL spend_private) aborts",
          "[C-API WalletStealthReceiver][precondition]") {
    kth_binary_mut_t filter = kth_core_binary_construct_default();
    KTH_EXPECT_ABORT(kth_wallet_stealth_receiver_construct(
        &kScanPrivate, NULL, filter, kMainnetP2KH));
    kth_core_binary_destruct(filter);
}

TEST_CASE("C-API wallet::stealth_receiver - construct(NULL filter) aborts",
          "[C-API WalletStealthReceiver][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_receiver_construct(
        &kScanPrivate, &kSpendPrivate, NULL, kMainnetP2KH));
}

TEST_CASE("C-API wallet::stealth_receiver - valid(NULL) aborts",
          "[C-API WalletStealthReceiver][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_receiver_valid(NULL));
}

TEST_CASE("C-API wallet::stealth_receiver - stealth_address(NULL) aborts",
          "[C-API WalletStealthReceiver][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_receiver_stealth_address(NULL));
}
