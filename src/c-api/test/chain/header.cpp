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
#include <time.h>

#include <kth/capi/chain/header.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint32_t const kVersion   = 10u;
static uint32_t const kTimestamp = 531234u;
static uint32_t const kBits      = 6523454u;
static uint32_t const kNonce     = 68644u;

// 32-byte hashes used throughout the suite, written here as raw byte arrays
// so the tests do not depend on any C++ helper.
static uint8_t const kPrevHash[32] = {
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t const kMerkle[32] = {
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a
};

static kth_hash_t make_hash(uint8_t const* bytes) {
    kth_hash_t h;
    memcpy(h.hash, bytes, KTH_BITCOIN_HASH_SIZE);
    return h;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - default construct is invalid", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_chain_header_is_valid(header) == 0);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - field constructor preserves all fields", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct(
        kVersion, kPrevHash, kMerkle, kTimestamp, kBits, kNonce);

    REQUIRE(kth_chain_header_is_valid(header) != 0);
    REQUIRE(kth_chain_header_version(header)   == kVersion);
    REQUIRE(kth_chain_header_timestamp(header) == kTimestamp);
    REQUIRE(kth_chain_header_bits(header)      == kBits);
    REQUIRE(kth_chain_header_nonce(header)     == kNonce);

    kth_hash_t prev = kth_chain_header_previous_block_hash(header);
    REQUIRE(kth_hash_equal(prev, make_hash(kPrevHash)) != 0);

    kth_hash_t merkle = kth_chain_header_merkle(header);
    REQUIRE(kth_hash_equal(merkle, make_hash(kMerkle)) != 0);

    kth_chain_header_destruct(header);
}

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - from_data insufficient bytes fails", "[C-API Header]") {
    uint8_t data[10];
    memset(data, 0, sizeof(data));
    kth_header_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_header_construct_from_data(data, 10, 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Header - to_data / from_data roundtrip", "[C-API Header]") {
    kth_header_mut_t expected = kth_chain_header_construct(
        kVersion, kPrevHash, kMerkle, kTimestamp, kBits, kNonce);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_header_to_data(expected, 1, &size);
    REQUIRE(size == 80u);
    REQUIRE(raw != NULL);

    kth_header_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_header_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);

    REQUIRE(kth_chain_header_is_valid(parsed) != 0);
    REQUIRE(kth_chain_header_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_header_destruct(parsed);
    kth_chain_header_destruct(expected);
}

// ---------------------------------------------------------------------------
// Getters / setters
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - version setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_chain_header_version(header) != 4521u);
    kth_chain_header_set_version(header, 4521u);
    REQUIRE(kth_chain_header_version(header) == 4521u);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - previous_block_hash setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_hash_is_null(kth_chain_header_previous_block_hash(header)) != 0);

    kth_chain_header_set_previous_block_hash(header, kPrevHash);
    REQUIRE(kth_hash_equal(kth_chain_header_previous_block_hash(header),
                           make_hash(kPrevHash)) != 0);

    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - merkle setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_hash_is_null(kth_chain_header_merkle(header)) != 0);

    kth_chain_header_set_merkle(header, kMerkle);
    REQUIRE(kth_hash_equal(kth_chain_header_merkle(header),
                           make_hash(kMerkle)) != 0);

    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - timestamp setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_chain_header_timestamp(header) != 4521u);
    kth_chain_header_set_timestamp(header, 4521u);
    REQUIRE(kth_chain_header_timestamp(header) == 4521u);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - bits setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_chain_header_bits(header) != 4521u);
    kth_chain_header_set_bits(header, 4521u);
    REQUIRE(kth_chain_header_bits(header) == 4521u);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - nonce setter roundtrip", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    REQUIRE(kth_chain_header_nonce(header) != 4521u);
    kth_chain_header_set_nonce(header, 4521u);
    REQUIRE(kth_chain_header_nonce(header) == 4521u);
    kth_chain_header_destruct(header);
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - is_valid_timestamp now true", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    time_t now = time(NULL);
    kth_chain_header_set_timestamp(header, (uint32_t)now);
    REQUIRE(kth_chain_header_is_valid_timestamp(header) != 0);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - is_valid_timestamp 3h in future false", "[C-API Header]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    time_t future = time(NULL) + 3 * 60 * 60;
    kth_chain_header_set_timestamp(header, (uint32_t)future);
    REQUIRE(kth_chain_header_is_valid_timestamp(header) == 0);
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - is_valid_proof_of_work bits exceed max false", "[C-API Header]") {
    // Any bits value above the proof-of-work limit should make the header
    // fail validation. Use a sentinel value larger than any plausible target.
    kth_header_mut_t header = kth_chain_header_construct_default();
    kth_chain_header_set_bits(header, 0xFFFFFFFFu);
    REQUIRE(kth_chain_header_is_valid_proof_of_work(header, 1) == 0);
    kth_chain_header_destruct(header);
}

// ---------------------------------------------------------------------------
// Copy / equals
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - copy preserves all fields", "[C-API Header]") {
    kth_header_mut_t original = kth_chain_header_construct(
        kVersion, kPrevHash, kMerkle, kTimestamp, kBits, kNonce);
    kth_header_mut_t copy = kth_chain_header_copy(original);

    REQUIRE(kth_chain_header_is_valid(copy) != 0);
    REQUIRE(kth_chain_header_equals(original, copy) != 0);
    REQUIRE(kth_chain_header_version(copy)   == kVersion);
    REQUIRE(kth_chain_header_timestamp(copy) == kTimestamp);
    REQUIRE(kth_chain_header_bits(copy)      == kBits);
    REQUIRE(kth_chain_header_nonce(copy)     == kNonce);

    kth_chain_header_destruct(copy);
    kth_chain_header_destruct(original);
}

TEST_CASE("C-API Header - equals duplicates", "[C-API Header]") {
    kth_header_mut_t a = kth_chain_header_construct(
        kVersion, kPrevHash, kMerkle, kTimestamp, kBits, kNonce);
    kth_header_mut_t b = kth_chain_header_construct(
        kVersion, kPrevHash, kMerkle, kTimestamp, kBits, kNonce);
    kth_header_mut_t c = kth_chain_header_construct_default();

    REQUIRE(kth_chain_header_equals(a, b) != 0);
    REQUIRE(kth_chain_header_equals(a, c) == 0);

    kth_chain_header_destruct(a);
    kth_chain_header_destruct(b);
    kth_chain_header_destruct(c);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - satoshi_fixed_size is 80", "[C-API Header]") {
    REQUIRE(kth_chain_header_satoshi_fixed_size() == 80u);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Header - construct_from_data null data with non-zero size aborts",
          "[C-API Header][precondition]") {
    KTH_EXPECT_ABORT({
        kth_header_mut_t out = NULL;
        kth_chain_header_construct_from_data(NULL, 1, 1, &out);
    });
}

TEST_CASE("C-API Header - construct_from_data NULL data with zero size returns error",
          "[C-API Header]") {
    // (NULL, 0) is a valid empty input; the function should not abort. The
    // header parser will fail because zero bytes are insufficient, but it
    // must do so gracefully via an error code.
    kth_header_mut_t out = NULL;
    kth_error_code_t ec = kth_chain_header_construct_from_data(NULL, 0, 1, &out);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out == NULL);
}

TEST_CASE("C-API Header - construct null previous_block_hash aborts",
          "[C-API Header][precondition]") {
    KTH_EXPECT_ABORT(
        kth_chain_header_construct(kVersion, NULL, kMerkle,
                                   kTimestamp, kBits, kNonce));
}

TEST_CASE("C-API Header - construct null merkle aborts",
          "[C-API Header][precondition]") {
    KTH_EXPECT_ABORT(
        kth_chain_header_construct(kVersion, kPrevHash, NULL,
                                   kTimestamp, kBits, kNonce));
}

TEST_CASE("C-API Header - to_data null out_size aborts",
          "[C-API Header][precondition]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    KTH_EXPECT_ABORT(kth_chain_header_to_data(header, 1, NULL));
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - set_previous_block_hash null aborts",
          "[C-API Header][precondition]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    KTH_EXPECT_ABORT(kth_chain_header_set_previous_block_hash(header, NULL));
    kth_chain_header_destruct(header);
}

TEST_CASE("C-API Header - set_merkle null aborts",
          "[C-API Header][precondition]") {
    kth_header_mut_t header = kth_chain_header_construct_default();
    KTH_EXPECT_ABORT(kth_chain_header_set_merkle(header, NULL));
    kth_chain_header_destruct(header);
}
