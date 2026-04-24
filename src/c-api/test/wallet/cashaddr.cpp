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
#include <stdlib.h>
#include <string.h>

#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/cashaddr.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Case-insensitive string compare. cashaddr encodings round-trip up
// to case — the encoder normalises to lowercase, the decoder accepts
// either. Used in the round-trip checks below.
// ---------------------------------------------------------------------------

static int cashaddr_iequal(char const* a, char const* b) {
    size_t const na = strlen(a);
    size_t const nb = strlen(b);
    if (na != nb) return 0;
    for (size_t i = 0; i < na; ++i) {
        char ca = a[i]; if (ca >= 'A' && ca <= 'Z') ca -= ('A' - 'a');
        char cb = b[i]; if (cb >= 'A' && cb <= 'Z') cb -= ('A' - 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// encode — raw payload + prefix round-trips through decode
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::cashaddr - encode + decode round-trip",
          "[C-API WalletCashaddr][round_trip]") {
    // Lifted from the domain suite's `cashaddr_rawencode` test:
    // prefix "helloworld", payload {0x1f, 0x0d}. The decoded payload
    // must be byte-identical and the prefix must round-trip.
    uint8_t const payload[] = { 0x1fu, 0x0du };
    char* encoded = kth_wallet_cashaddr_encode("helloworld", payload, 2u);
    REQUIRE(encoded != NULL);
    REQUIRE(strlen(encoded) > 0u);

    uint8_t* out_payload = NULL;
    kth_size_t out_size = 0u;
    char* out_prefix = kth_wallet_cashaddr_decode(
        encoded, /*default_prefix=*/"", &out_payload, &out_size);
    REQUIRE(out_prefix != NULL);
    REQUIRE(strcmp(out_prefix, "helloworld") == 0);
    REQUIRE(out_size == 2u);
    REQUIRE(out_payload != NULL);
    REQUIRE(out_payload[0] == 0x1fu);
    REQUIRE(out_payload[1] == 0x0du);

    kth_core_destruct_array(out_payload);
    kth_core_destruct_string(out_prefix);
    kth_core_destruct_string(encoded);
}

TEST_CASE("C-API wallet::cashaddr - known-good BCH prefix/payload roundtrips",
          "[C-API WalletCashaddr][round_trip]") {
    // cashaddr test vector from the domain suite's `cashaddr_test-
    // vectors_valid`: a mainnet P2PKH address, recognisable by the
    // `bitcoincash:` prefix + `q` HRP nibble.
    char const* kGood =
        "bitcoincash:qpzry9x8gf2tvdw0s3jn54khce6mua7lcw20ayyn";

    uint8_t* out_payload = NULL;
    kth_size_t out_size = 0u;
    char* out_prefix = kth_wallet_cashaddr_decode(
        kGood, "", &out_payload, &out_size);
    REQUIRE(out_prefix != NULL);
    REQUIRE(strcmp(out_prefix, "bitcoincash") == 0);
    REQUIRE(out_size > 0u);
    REQUIRE(out_payload != NULL);

    // Re-encode and compare case-insensitively — the encoder
    // canonicalises to lowercase, so the string must match the
    // original when compared under case-fold.
    char* recoded = kth_wallet_cashaddr_encode(out_prefix, out_payload, out_size);
    REQUIRE(recoded != NULL);
    REQUIRE(cashaddr_iequal(recoded, kGood) == 1);

    kth_core_destruct_string(recoded);
    kth_core_destruct_array(out_payload);
    kth_core_destruct_string(out_prefix);
}

// ---------------------------------------------------------------------------
// decode — case-insensitive input, invalid strings, empty payload
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::cashaddr - uppercase decode matches lowercase",
          "[C-API WalletCashaddr][case]") {
    // Per BIP cashaddr spec the encoding is case-insensitive as long
    // as the caller doesn't mix cases within a single string.
    uint8_t* lower_payload = NULL;
    kth_size_t lower_size = 0u;
    char* lower_prefix = kth_wallet_cashaddr_decode(
        "prefix:x64nx6hz", "", &lower_payload, &lower_size);
    REQUIRE(lower_prefix != NULL);
    // Ground-truth check: without this, the test would pass even if
    // BOTH decodes failed (empty prefixes compare case-insensitively
    // equal, zero sizes match, and memcmp over zero bytes is always
    // 0). Require a non-empty decode on the known-good input so any
    // regression in the decoder trips here.
    REQUIRE(strlen(lower_prefix) > 0u);

    uint8_t* upper_payload = NULL;
    kth_size_t upper_size = 0u;
    char* upper_prefix = kth_wallet_cashaddr_decode(
        "PREFIX:X64NX6HZ", "", &upper_payload, &upper_size);
    REQUIRE(upper_prefix != NULL);

    REQUIRE(cashaddr_iequal(lower_prefix, upper_prefix) == 1);
    REQUIRE(lower_size == upper_size);
    REQUIRE(memcmp(lower_payload, upper_payload, lower_size) == 0);

    kth_core_destruct_array(upper_payload);
    kth_core_destruct_string(upper_prefix);
    kth_core_destruct_array(lower_payload);
    kth_core_destruct_string(lower_prefix);
}

TEST_CASE("C-API wallet::cashaddr - invalid string returns empty prefix",
          "[C-API WalletCashaddr][decode]") {
    // The domain contract: a malformed input produces an empty-string
    // prefix (not NULL — the owned empty string still needs freeing).
    // Checksum tweak: the `64` digits of a valid vector changed to
    // `32` (`prefix:x64nx6hz` → `prefix:x32nx6hz`) keeps the layout
    // well-formed but fails the bech32 polymod.
    uint8_t* out_payload = NULL;
    kth_size_t out_size = 0u;
    char* out_prefix = kth_wallet_cashaddr_decode(
        "prefix:x32nx6hz", "", &out_payload, &out_size);
    REQUIRE(out_prefix != NULL);
    REQUIRE(strlen(out_prefix) == 0u);

    kth_core_destruct_array(out_payload);
    kth_core_destruct_string(out_prefix);
}

TEST_CASE("C-API wallet::cashaddr - encode(empty payload) is callable",
          "[C-API WalletCashaddr][encode]") {
    // Empty payloads are legal at the encode layer (the resulting
    // string is minimal but still well-formed). Pointer may be NULL
    // when `n == 0` per the encode precondition.
    char* encoded = kth_wallet_cashaddr_encode("p", NULL, 0u);
    REQUIRE(encoded != NULL);
    REQUIRE(strlen(encoded) > 0u);
    kth_core_destruct_string(encoded);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API wallet::cashaddr - encode(NULL prefix) aborts",
          "[C-API WalletCashaddr][precondition]") {
    uint8_t const payload[] = { 0x00u };
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_encode(NULL, payload, 1u));
}

TEST_CASE("C-API wallet::cashaddr - encode(NULL payload, n!=0) aborts",
          "[C-API WalletCashaddr][precondition]") {
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_encode("p", NULL, 1u));
}

TEST_CASE("C-API wallet::cashaddr - decode(NULL str) aborts",
          "[C-API WalletCashaddr][precondition]") {
    uint8_t* p = NULL;
    kth_size_t n = 0u;
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_decode(NULL, "", &p, &n));
}

TEST_CASE("C-API wallet::cashaddr - decode(NULL default_prefix) aborts",
          "[C-API WalletCashaddr][precondition]") {
    uint8_t* p = NULL;
    kth_size_t n = 0u;
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_decode("prefix:x64nx6hz", NULL, &p, &n));
}

TEST_CASE("C-API wallet::cashaddr - decode(NULL out_payload) aborts",
          "[C-API WalletCashaddr][precondition]") {
    kth_size_t n = 0u;
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_decode("prefix:x64nx6hz", "", NULL, &n));
}

TEST_CASE("C-API wallet::cashaddr - decode(NULL out_size) aborts",
          "[C-API WalletCashaddr][precondition]") {
    uint8_t* p = NULL;
    KTH_EXPECT_ABORT(kth_wallet_cashaddr_decode("prefix:x64nx6hz", "", &p, NULL));
}
