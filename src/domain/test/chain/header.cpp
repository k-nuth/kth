// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================
// These tests verify that header operations work at compile-time in C++23.
// If any of these fail, compilation will fail with an error.

namespace {

// Test data for compile-time tests
constexpr hash_digest ct_prev_hash = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

constexpr hash_digest ct_merkle = {{
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a
}};

// Test: Default constructor is constexpr
constexpr chain::header ct_default_header{};
static_assert( ! ct_default_header.is_valid(), "default header should be invalid");
static_assert(ct_default_header.version() == 0, "default version should be 0");
static_assert(ct_default_header.timestamp() == 0, "default timestamp should be 0");
static_assert(ct_default_header.bits() == 0, "default bits should be 0");
static_assert(ct_default_header.nonce() == 0, "default nonce should be 0");
// static_assert(ct_default_header.median_time_past() == 0, "default mtp should be 0");

// Test: Field constructor is constexpr
constexpr chain::header ct_header_from_fields{
    1u,           // version
    ct_prev_hash, // previous_block_hash
    ct_merkle,    // merkle
    1231006505u,  // timestamp (Bitcoin genesis)
    0x1d00ffffu,  // bits
    2083236893u   // nonce
};
static_assert(ct_header_from_fields.is_valid(), "constructed header should be valid");
static_assert(ct_header_from_fields.version() == 1u, "version mismatch");
static_assert(ct_header_from_fields.timestamp() == 1231006505u, "timestamp mismatch");
static_assert(ct_header_from_fields.bits() == 0x1d00ffffu, "bits mismatch");
static_assert(ct_header_from_fields.nonce() == 2083236893u, "nonce mismatch");
// static_assert(ct_header_from_fields.median_time_past() == 0, "mtp should be 0");

// Test: Constructor with median_time_past is constexpr
// constexpr chain::header ct_header_with_mtp{
//     1u, ct_prev_hash, ct_merkle, 1231006505u, 0x1d00ffffu, 2083236893u, 12345u
// };
// static_assert(ct_header_with_mtp.median_time_past() == 12345u, "mtp mismatch");

// Test: Copy constructor is constexpr
constexpr chain::header ct_header_copy{ct_header_from_fields};
static_assert(ct_header_copy.version() == ct_header_from_fields.version(), "copy version mismatch");
static_assert(ct_header_copy.timestamp() == ct_header_from_fields.timestamp(), "copy timestamp mismatch");

// Test: Equality operator is constexpr
static_assert(ct_header_copy == ct_header_from_fields, "copy should equal original");
static_assert( ! (ct_header_copy != ct_header_from_fields), "copy should not be unequal to original");
static_assert(ct_default_header != ct_header_from_fields, "default should not equal constructed");

// Test: previous_block_hash getter is constexpr
static_assert(ct_header_from_fields.previous_block_hash() == ct_prev_hash, "prev_hash mismatch");

// Test: merkle getter is constexpr
static_assert(ct_header_from_fields.merkle() == ct_merkle, "merkle mismatch");

// Test: serialized_size is constexpr
static_assert(ct_header_from_fields.serialized_size(true) == 80, "wire size should be 80");
static_assert(ct_header_from_fields.serialized_size(false) == 80, "non-wire size should be 80");

// Test: satoshi_fixed_size is constexpr
static_assert(chain::header::satoshi_fixed_size() == 80, "fixed size should be 80");

// Test: raw_data span is constexpr
constexpr auto ct_raw_data = ct_header_from_fields.raw_data();
static_assert(ct_raw_data.size() == 80, "raw_data size should be 80");

// Test: Field spans are constexpr
constexpr auto ct_version_span = ct_header_from_fields.version_span();
static_assert(ct_version_span.size() == 4, "version_span size should be 4");

constexpr auto ct_prev_hash_span = ct_header_from_fields.previous_block_hash_span();
static_assert(ct_prev_hash_span.size() == 32, "previous_block_hash_span size should be 32");

constexpr auto ct_merkle_span = ct_header_from_fields.merkle_span();
static_assert(ct_merkle_span.size() == 32, "merkle_span size should be 32");

constexpr auto ct_timestamp_span = ct_header_from_fields.timestamp_span();
static_assert(ct_timestamp_span.size() == 4, "timestamp_span size should be 4");

constexpr auto ct_bits_span = ct_header_from_fields.bits_span();
static_assert(ct_bits_span.size() == 4, "bits_span size should be 4");

constexpr auto ct_nonce_span = ct_header_from_fields.nonce_span();
static_assert(ct_nonce_span.size() == 4, "nonce_span size should be 4");

// Test: Raw byte array constructor is constexpr
constexpr std::array<uint8_t, 80> ct_raw_bytes = {{
    // version (little-endian): 1
    0x01, 0x00, 0x00, 0x00,
    // previous block hash (32 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // merkle root (32 bytes)
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
    // timestamp (little-endian): 1231006505
    0x29, 0xab, 0x5f, 0x49,
    // bits (little-endian): 0x1d00ffff
    0xff, 0xff, 0x00, 0x1d,
    // nonce (little-endian): 2083236893
    0x1d, 0xac, 0x2b, 0x7c
}};
constexpr chain::header ct_header_from_bytes{ct_raw_bytes};
static_assert(ct_header_from_bytes.version() == 1, "version from bytes mismatch");
static_assert(ct_header_from_bytes.timestamp() == 1231006505u, "timestamp from bytes mismatch");
static_assert(ct_header_from_bytes.bits() == 0x1d00ffffu, "bits from bytes mismatch");
static_assert(ct_header_from_bytes.nonce() == 2083236893u, "nonce from bytes mismatch");

// Test: Endianness round-trip at compile time
// Create header from fields, verify raw bytes match expected layout
constexpr chain::header ct_endian_test{2u, null_hash, null_hash, 3u, 4u, 5u};
static_assert(ct_endian_test.version() == 2u, "endian test version");
static_assert(ct_endian_test.timestamp() == 3u, "endian test timestamp");
static_assert(ct_endian_test.bits() == 4u, "endian test bits");
static_assert(ct_endian_test.nonce() == 5u, "endian test nonce");

// Verify little-endian encoding in raw_data
constexpr auto ct_endian_raw = ct_endian_test.raw_data();
// version = 2 should be 0x02, 0x00, 0x00, 0x00 in little-endian
static_assert(ct_endian_raw[0] == 0x02, "version byte 0");
static_assert(ct_endian_raw[1] == 0x00, "version byte 1");
static_assert(ct_endian_raw[2] == 0x00, "version byte 2");
static_assert(ct_endian_raw[3] == 0x00, "version byte 3");

// Test: Constexpr lambda to build header (C++20+)
constexpr auto make_test_header = []() {
    return chain::header{42u, null_hash, null_hash, 100u, 200u, 300u};
};
constexpr chain::header ct_lambda_header = make_test_header();
static_assert(ct_lambda_header.version() == 42u, "lambda header version");

} // anonymous namespace

// =============================================================================
// Runtime tests (Catch2)
// =============================================================================

// Start Test Suite: chain header tests

TEST_CASE("chain header constructor 1 always initialized invalid", "[chain header]") {
    chain::header instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("chain header  constructor 2  always  equals params", "[chain header]") {
    uint32_t const version = 10u;
    auto const previous = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
    auto const merkle = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    uint32_t const timestamp = 531234u;
    uint32_t const bits = 6523454u;
    uint32_t const nonce = 68644u;

    chain::header instance(version, previous, merkle, timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("chain header  constructor 3  always  equals params", "[chain header]") {
    uint32_t const version = 10u;
    uint32_t const timestamp = 531234u;
    uint32_t const bits = 6523454u;
    uint32_t const nonce = 68644u;

    // These must be non-const.
    auto previous = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;
    auto merkle = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;

    chain::header instance(version, std::move(previous), std::move(merkle), timestamp, bits, nonce);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(bits == instance.bits());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(previous == instance.previous_block_hash());
    REQUIRE(merkle == instance.merkle());
}

TEST_CASE("chain header  constructor 4  always  equals params", "[chain header]") {
    chain::header const expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("chain header  constructor 5  always  equals params", "[chain header]") {
    // This must be non-const.
    chain::header expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("chain header from data insufficient bytes  failure", "[chain header]") {
    data_chunk data(10);
    byte_reader reader(data);
    auto const result = chain::header::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("chain header from data valid input  success", "[chain header]") {
    chain::header expected{
        10,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();

    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  factory from data 2  valid input  success", "[chain header]") {
    chain::header expected{
        10,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();
    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  factory from data 3  valid input  success", "[chain header]") {
    chain::header const expected{
        10,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234,
        6523454,
        68644};

    auto const data = expected.to_data();
    byte_reader reader(data);
    auto const result_exp = chain::header::from_data(reader);
    REQUIRE(result_exp);
    auto const& result = *result_exp;

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
}

TEST_CASE("chain header  version accessor  always  returns initialized value", "[chain header]") {
    uint32_t const value = 11234u;
    chain::header const instance(
        value,
        "abababababababababababababababababababababababababababababababab"_hash,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.version());
}

// Note: setter tests removed - header is now immutable

TEST_CASE("chain header  previous block hash accessor 1  always  returns initialized value", "[chain header]") {
    auto const value = "abababababababababababababababababababababababababababababababab"_hash;
    chain::header instance(
        11234u,
        value,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.previous_block_hash());
}

TEST_CASE("chain header  previous block hash accessor 2  always  returns initialized value", "[chain header]") {
    auto const value = "abababababababababababababababababababababababababababababababab"_hash;
    chain::header const instance(
        11234u,
        value,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.previous_block_hash());
}

TEST_CASE("chain header  merkle accessor 1  always  returns initialized value", "[chain header]") {
    auto const value = "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash;
    chain::header instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        value,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.merkle());
}

TEST_CASE("chain header  merkle accessor 2  always  returns initialized value", "[chain header]") {
    auto const value = "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash;
    chain::header const instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        value,
        753234u,
        4356344u,
        34564u);

    REQUIRE(value == instance.merkle());
}

TEST_CASE("chain header  timestamp accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 753234u;
    chain::header instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        value,
        4356344u,
        34564u);

    REQUIRE(value == instance.timestamp());
}

TEST_CASE("chain header  bits accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 4356344u;
    chain::header instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        value,
        34564u);

    REQUIRE(value == instance.bits());
}

TEST_CASE("chain header  nonce accessor  always  returns initialized value", "[chain header]") {
    uint32_t value = 34564u;
    chain::header instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        4356344u,
        value);

    REQUIRE(value == instance.nonce());
}

TEST_CASE("chain header  proof1  genesis mainnet  expected", "[chain header]") {
    REQUIRE(chain::header::proof(0x1d00ffff) == 0x0000000100010001);
}

TEST_CASE("chain header  is valid proof of work  bits exceeds maximum  returns false", "[chain header]") {
    chain::header const instance{
        1u, null_hash, null_hash, 0u,
        retarget_proof_of_work_limit + 1, 0u
    };
    auto const hdr_hash = hash(instance);
    REQUIRE( ! instance.is_valid_proof_of_work(hdr_hash));
}

TEST_CASE("chain header  is valid proof of work  retarget bits exceeds maximum  returns false", "[chain header]") {
    chain::header const instance{
        1u, null_hash, null_hash, 0u,
        no_retarget_proof_of_work_limit + 1, 0u
    };
    auto const hdr_hash = hash(instance);
    REQUIRE( ! instance.is_valid_proof_of_work(hdr_hash, false));
}

TEST_CASE("chain header  is valid proof of work  hash greater bits  returns false", "[chain header]") {
    chain::header const instance(
        11234u,
        "abababababababababababababababababababababababababababababababab"_hash,
        "fefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefe"_hash,
        753234u,
        0u,
        34564u);

    auto const hdr_hash = hash(instance);
    REQUIRE( ! instance.is_valid_proof_of_work(hdr_hash));
}

TEST_CASE("chain header  is valid proof of work  hash less than bits  returns true", "[chain header]") {
    chain::header const instance(
        4u,
        "000000000000000003ddc1e929e2944b8b0039af9aa0d826c480a83d8b39c373"_hash,
        "a6cb0b0d6531a71abe2daaa4a991e5498e1b6b0b51549568d0f9d55329b905df"_hash,
        1474388414u,
        402972254u,
        2842832236u);

    auto const hdr_hash = hash(instance);
    REQUIRE(instance.is_valid_proof_of_work(hdr_hash));
}

TEST_CASE("chain header  operator assign equals  always  matches equivalent", "[chain header]") {
    // This must be non-const.
    chain::header value(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    REQUIRE(value.is_valid());

    chain::header instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("chain header  operator boolean equals  duplicates  returns true", "[chain header]") {
    chain::header const expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("chain header  operator boolean equals  differs  returns false", "[chain header]") {
    chain::header const expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance;
    REQUIRE(instance != expected);
}

TEST_CASE("chain header  operator boolean not equals  duplicates  returns false", "[chain header]") {
    chain::header const expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("chain header  operator boolean not equals  differs  returns true", "[chain header]") {
    chain::header const expected(
        10u,
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        531234u,
        6523454u,
        68644u);

    chain::header instance;
    REQUIRE(instance != expected);
}

// End Test Suite
