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
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(kFullUri, 1, &u) == kth_ec_success);
    REQUIRE(u != NULL);

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
    // else is rejected — `parse_from` reports a non-success error code
    // and leaves the OUT handle untouched (still NULL) so the caller
    // can distinguish a parse failure from a successful parse of a
    // blank URI.
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from("http://example.com", 1, &u) != kth_ec_success);
    REQUIRE(u == NULL);
}

TEST_CASE("C-API BitcoinURI - bare scheme parses",
          "[C-API BitcoinURI][parse]") {
    // BIP21 allows an empty payload — `bitcoin:` alone is a legal URI.
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from("bitcoin:", 1, &u) == kth_ec_success);
    REQUIRE(u != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_amount(u) == 0u);
    kth_wallet_bitcoin_uri_destruct(u);
}

// ---------------------------------------------------------------------------
// to_string round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - to_string round-trips through parse_from",
          "[C-API BitcoinURI][encode]") {
    // Parse → serialize → parse again yields an equal URI. Query-param
    // ordering is stable (lexicographic in the current impl) so we can
    // compare via `equals` on the re-parsed handles.
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(kFullUri, 1, &u) == kth_ec_success);
    REQUIRE(u != NULL);

    char* encoded = kth_wallet_bitcoin_uri_to_string(u);
    REQUIRE(encoded != NULL);

    kth_bitcoin_uri_mut_t reparsed = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(encoded, 1, &reparsed) == kth_ec_success);
    REQUIRE(reparsed != NULL);
    REQUIRE(kth_wallet_bitcoin_uri_equals(u, reparsed) != 0);

    kth_wallet_bitcoin_uri_destruct(reparsed);
    kth_core_destruct_string(encoded);
    kth_wallet_bitcoin_uri_destruct(u);
}

// ---------------------------------------------------------------------------
// Address extraction
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - payment extracts a valid handle from a P2PKH path",
          "[C-API BitcoinURI][address]") {
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(kFullUri, 1, &u) == kth_ec_success);
    REQUIRE(u != NULL);

    kth_payment_address_mut_t pa = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_payment(u, &pa) == kth_ec_success);
    REQUIRE(pa != NULL);
    kth_wallet_payment_address_destruct(pa);

    kth_wallet_bitcoin_uri_destruct(u);
}

TEST_CASE("C-API BitcoinURI - payment on params-only URI is an error",
          "[C-API BitcoinURI][address]") {
    // No address in the path → `payment()` can't build a payment_address.
    // The OUT handle stays NULL, error code is non-success.
    kth_bitcoin_uri_mut_t u = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from("bitcoin:?amount=0.001", 1, &u) == kth_ec_success);
    REQUIRE(u != NULL);

    kth_payment_address_mut_t pa = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_payment(u, &pa) != kth_ec_success);
    REQUIRE(pa == NULL);

    kth_wallet_bitcoin_uri_destruct(u);
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - equals two identically-parsed URIs",
          "[C-API BitcoinURI][equality]") {
    kth_bitcoin_uri_mut_t a = NULL;
    kth_bitcoin_uri_mut_t b = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(kFullUri, 1, &a) == kth_ec_success);
    REQUIRE(kth_wallet_bitcoin_uri_parse_from(kFullUri, 1, &b) == kth_ec_success);
    REQUIRE(kth_wallet_bitcoin_uri_equals(a, b) != 0);
    REQUIRE(kth_wallet_bitcoin_uri_not_equal(a, b) == 0);
    kth_wallet_bitcoin_uri_destruct(b);
    kth_wallet_bitcoin_uri_destruct(a);
}

TEST_CASE("C-API BitcoinURI - differing amounts compare unequal",
          "[C-API BitcoinURI][equality]") {
    kth_bitcoin_uri_mut_t a = NULL;
    kth_bitcoin_uri_mut_t b = NULL;
    REQUIRE(kth_wallet_bitcoin_uri_parse_from("bitcoin:?amount=0.001", 1, &a) == kth_ec_success);
    REQUIRE(kth_wallet_bitcoin_uri_parse_from("bitcoin:?amount=0.002", 1, &b) == kth_ec_success);
    REQUIRE(kth_wallet_bitcoin_uri_equals(a, b) == 0);
    REQUIRE(kth_wallet_bitcoin_uri_not_equal(a, b) != 0);
    kth_wallet_bitcoin_uri_destruct(b);
    kth_wallet_bitcoin_uri_destruct(a);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API BitcoinURI - copy null aborts",
          "[C-API BitcoinURI][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_bitcoin_uri_copy(NULL));
}
