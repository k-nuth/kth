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
#include <kth/capi/wallet/stealth_address.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Known-good fixtures (lifted from the domain suite) — both cover
// scan-only and scan+spend variants on mainnet / testnet.
// ---------------------------------------------------------------------------

static char const* const kScanMainnet =
    "vJmzLu29obZcUGXXgotapfQLUpz7dfnZpbr4xg1R75qctf8xaXAteRdi3ZUk3T2ZMSad5KyPbve7uyH6eswYAxLHRVSbWgNUeoGuXp";

static char const* const kScanTestnet =
    "waPXhQwQE9tDugfgLkvpDs3dnkPx1RsfDjFt4zBq7EeWeATRHpyQpYrFZR8T4BQy91Vpvshm2TDER8b9ZryuZ8VSzz8ywzNzX8NqF4";

static char const* const kScanPubOnlyMainnet =
    "hfFGUXFPKkQ5M6LC6aEUKMsURdhw93bUdYdacEtBA8XttLv7evZkira2i";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API StealthAddress - default construct is invalid",
          "[C-API StealthAddress][lifecycle]") {
    kth_stealth_address_mut_t a = kth_wallet_stealth_address_construct_default();
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(a) == 0);
    kth_wallet_stealth_address_destruct(a);
}

TEST_CASE("C-API StealthAddress - destruct(NULL) is a no-op",
          "[C-API StealthAddress][lifecycle]") {
    kth_wallet_stealth_address_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Encoded string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API StealthAddress - encoded round-trips through construct_from_encoded (mainnet)",
          "[C-API StealthAddress][encode]") {
    kth_stealth_address_mut_t a = kth_wallet_stealth_address_construct_from_encoded(kScanMainnet);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(a) != 0);
    REQUIRE(kth_wallet_stealth_address_version(a) == 42u);

    char* back = kth_wallet_stealth_address_encoded(a);
    REQUIRE(back != NULL);
    REQUIRE(strcmp(back, kScanMainnet) == 0);
    kth_core_destruct_string(back);

    kth_wallet_stealth_address_destruct(a);
}

TEST_CASE("C-API StealthAddress - encoded round-trips through construct_from_encoded (testnet)",
          "[C-API StealthAddress][encode]") {
    // Testnet addresses carry version 43, separating them from the 42
    // of mainnet — exercise both so a future regression that hardcodes
    // a version byte anywhere in the codepath trips here.
    kth_stealth_address_mut_t a = kth_wallet_stealth_address_construct_from_encoded(kScanTestnet);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(a) != 0);
    REQUIRE(kth_wallet_stealth_address_version(a) == 43u);
    kth_wallet_stealth_address_destruct(a);
}

TEST_CASE("C-API StealthAddress - scan-only variant preserves version",
          "[C-API StealthAddress][encode]") {
    // Short variant (no spend keys) also hits the same parser path.
    kth_stealth_address_mut_t a = kth_wallet_stealth_address_construct_from_encoded(kScanPubOnlyMainnet);
    REQUIRE(a != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(a) != 0);
    REQUIRE(kth_wallet_stealth_address_version(a) == 42u);
    kth_wallet_stealth_address_destruct(a);
}

// ---------------------------------------------------------------------------
// Chunk round-trip (decoded bytes)
// ---------------------------------------------------------------------------

TEST_CASE("C-API StealthAddress - to_chunk / construct_from_decoded round-trips",
          "[C-API StealthAddress][encode]") {
    kth_stealth_address_mut_t orig = kth_wallet_stealth_address_construct_from_encoded(kScanMainnet);
    REQUIRE(orig != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(orig) != 0);

    kth_size_t size = 0;
    uint8_t* chunk = kth_wallet_stealth_address_to_chunk(orig, &size);
    REQUIRE(chunk != NULL);
    REQUIRE(size > 0);

    kth_stealth_address_mut_t parsed = kth_wallet_stealth_address_construct_from_decoded(chunk, size);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_wallet_stealth_address_valid(parsed) != 0);
    REQUIRE(kth_wallet_stealth_address_equals(orig, parsed) != 0);

    kth_wallet_stealth_address_destruct(parsed);
    kth_core_destruct_array(chunk);
    kth_wallet_stealth_address_destruct(orig);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API StealthAddress - copy produces an equal-but-independent handle",
          "[C-API StealthAddress][lifecycle]") {
    kth_stealth_address_mut_t a = kth_wallet_stealth_address_construct_from_encoded(kScanMainnet);
    REQUIRE(a != NULL);
    kth_stealth_address_mut_t b = kth_wallet_stealth_address_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(b != a);
    REQUIRE(kth_wallet_stealth_address_equals(a, b) != 0);

    kth_wallet_stealth_address_destruct(b);
    REQUIRE(kth_wallet_stealth_address_valid(a) != 0);  // source survives copy destruct

    kth_wallet_stealth_address_destruct(a);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API StealthAddress - copy null aborts",
          "[C-API StealthAddress][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_address_copy(NULL));
}

TEST_CASE("C-API StealthAddress - version null aborts",
          "[C-API StealthAddress][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_stealth_address_version(NULL));
}
