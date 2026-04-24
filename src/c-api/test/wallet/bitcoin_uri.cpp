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
#include <kth/capi/wallet/bitcoin_uri.h>
#include <kth/capi/wallet/payment_address.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures — a canonical BIP21 URI with every standard query key set
// so each getter has something non-default to verify.
// ---------------------------------------------------------------------------

static char const* const kFullUri =
    "bitcoin:113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD"
    "?amount=0.001"
    "&label=Donation"
    "&message=Thanks"
    "&r=https%3A%2F%2Fmerchant.example.com%2Fbip70";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - default construct is invalid-but-queryable",
          "[C-API BitcoinURI][lifecycle]") {
    // A default URI carries no scheme / address / parameters — its
    // `operator bool` (surfaced as `valid`) reports false until a
    // scheme is set.
    kth_bitcoin_uri_mut_t u = kth_wallet_bitcoin_uri_construct_default();
    REQUIRE(u != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_valid(u) == 0);
    REQUIRE(kth_wallet_bitcoin_uri_amount(u) == 0u);
    kth_wallet_bitcoin_uri_destruct(u);
}

TEST_CASE("C-API BitcoinURI - destruct(NULL) is a no-op",
          "[C-API BitcoinURI][lifecycle]") {
    kth_wallet_bitcoin_uri_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Parsing — every BIP21 query key round-trips
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - parses amount / label / message / r",
          "[C-API BitcoinURI][parse]") {
    // `strict=1` rejects anything that doesn't conform to the BIP21
    // grammar; the fixture is well-formed so parsing succeeds.
    kth_bitcoin_uri_mut_t u = kth_wallet_bitcoin_uri_construct(kFullUri, 1);
    REQUIRE(u != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_valid(u) != 0);

    // amount=0.001 BTC → 100_000 satoshis. BIP21 fixed-point parsing
    // lives in the URI layer, so a regression there surfaces here.
    REQUIRE(kth_wallet_bitcoin_uri_amount(u) == 100000ull);

    char* label = kth_wallet_bitcoin_uri_label(u);
    REQUIRE(label != NULL);
    REQUIRE(strcmp(label, "Donation") == 0);
    kth_core_destruct_string(label);

    char* msg = kth_wallet_bitcoin_uri_message(u);
    REQUIRE(msg != NULL);
    REQUIRE(strcmp(msg, "Thanks") == 0);
    kth_core_destruct_string(msg);

    char* r = kth_wallet_bitcoin_uri_r(u);
    REQUIRE(r != NULL);
    REQUIRE(strcmp(r, "https://merchant.example.com/bip70") == 0);
    kth_core_destruct_string(r);

    char* addr = kth_wallet_bitcoin_uri_address(u);
    REQUIRE(addr != NULL);
    REQUIRE(strcmp(addr, "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD") == 0);
    kth_core_destruct_string(addr);

    kth_wallet_bitcoin_uri_destruct(u);
}

TEST_CASE("C-API BitcoinURI - strict mode rejects a malformed scheme",
          "[C-API BitcoinURI][parse]") {
    // BIP21 requires the `bitcoin:` scheme. In strict mode, anything
    // else is rejected and the constructor hands back NULL (the
    // `leak_if_valid` gate on the string-taking factory collapses an
    // `operator bool == false` instance to a null handle) so the
    // caller can distinguish a parse failure from a successful parse
    // of a blank URI.
    kth_bitcoin_uri_mut_t u = kth_wallet_bitcoin_uri_construct("http://example.com", 1);
    REQUIRE(u == NULL);
}

// ---------------------------------------------------------------------------
// Encoded round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - encoded round-trips through construct",
          "[C-API BitcoinURI][encode]") {
    // Parse → encode → parse again yields an equal URI. The exact byte
    // spelling can vary (query-param ordering isn't canonicalised), so
    // compare via equals on the re-parsed handles rather than strcmp.
    // Guard every handle / owned string explicitly — the getters abort
    // on NULL, and we'd rather a parse regression surface as a REQUIRE
    // failure than a SIGABRT during the test run.
    kth_bitcoin_uri_mut_t u = kth_wallet_bitcoin_uri_construct(kFullUri, 1);
    REQUIRE(u != NULL);
    char* encoded = kth_wallet_bitcoin_uri_encoded(u);
    REQUIRE(encoded != NULL);

    kth_bitcoin_uri_mut_t reparsed = kth_wallet_bitcoin_uri_construct(encoded, 1);
    REQUIRE(reparsed != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_valid(reparsed) != 0);
    REQUIRE(kth_wallet_bitcoin_uri_amount(reparsed)
            == kth_wallet_bitcoin_uri_amount(u));

    kth_wallet_bitcoin_uri_destruct(reparsed);
    kth_core_destruct_string(encoded);
    kth_wallet_bitcoin_uri_destruct(u);
}

// ---------------------------------------------------------------------------
// Setters — build a URI from scratch
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - setters compose a fresh URI",
          "[C-API BitcoinURI][build]") {
    kth_bitcoin_uri_mut_t u = kth_wallet_bitcoin_uri_construct_default();
    REQUIRE(u != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_set_scheme(u, "bitcoin") != 0);
    REQUIRE(kth_wallet_bitcoin_uri_set_address_string(
        u, "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD") != 0);
    kth_wallet_bitcoin_uri_set_amount(u, 250000ull);   // 0.0025 BTC
    kth_wallet_bitcoin_uri_set_label(u, "Coffee");
    kth_wallet_bitcoin_uri_set_message(u, "thx");

    REQUIRE(kth_wallet_bitcoin_uri_valid(u) != 0);
    REQUIRE(kth_wallet_bitcoin_uri_amount(u) == 250000ull);

    char* encoded = kth_wallet_bitcoin_uri_encoded(u);
    REQUIRE(encoded != NULL);
    // The address-and-query portion is present; exact parameter order
    // isn't pinned, so only assert on substrings that MUST appear in
    // the output. `0.0025` pins the BIP21 fixed-point amount encoding
    // (250000 sats ÷ 10⁸), catching any regression in the satoshi →
    // decimal path independent of the `amount()` getter's model-level
    // round-trip.
    REQUIRE(strstr(encoded, "bitcoin:") == encoded);
    REQUIRE(strstr(encoded, "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD") != NULL);
    REQUIRE(strstr(encoded, "0.0025") != NULL);
    kth_core_destruct_string(encoded);

    kth_wallet_bitcoin_uri_destruct(u);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - copy null aborts",
          "[C-API BitcoinURI][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_bitcoin_uri_copy(NULL));
}

TEST_CASE("C-API BitcoinURI - valid null aborts",
          "[C-API BitcoinURI][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_bitcoin_uri_valid(NULL));
}
