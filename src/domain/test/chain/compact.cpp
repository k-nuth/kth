// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdint>

#include <test_helpers.hpp>

// Start Test Suite: compact tests

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;

static auto const primes = "020305070b0d1113171d1f25292b2f353b3d4347494f53596165676b6d717f83"_hash;

static uint32_t factory(int32_t logical_exponent, bool negative, uint32_t mantissa) {
    // The exponent of a non-zero mantissa is valid from -3 to +29.
    KTH_ASSERT(logical_exponent >= -3 && logical_exponent <= 252);

    // The mantissa may not intrude on the sign bit or the exponent.
    KTH_ASSERT((mantissa & 0xff800000) == 0);

    // The logical 0 exponent is represented as 3, so consider that the decimal point.
    uint32_t const exponent = logical_exponent + 3;

    // Construct the non-normalized compact value.
    return exponent << 24 | (negative ? 1 : 0) << 23 | mantissa;
}

TEST_CASE("compact  constructor1  proof of work limit  normalizes unchanged", "[compact]") {
    REQUIRE(compact(retarget_proof_of_work_limit).normal() == retarget_proof_of_work_limit);
}

TEST_CASE("compact  constructor1  no retarget proof of work limit  normalizes unchanged", "[compact]") {
    REQUIRE(compact(no_retarget_proof_of_work_limit).normal() == no_retarget_proof_of_work_limit);
}

// constructor1/normal

TEST_CASE("compact  constructor1  negative3 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(-3, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(-3, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, false, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, false, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-3, false, 0x7fffff)).normal() == 0x00000000);
}

TEST_CASE("compact  constructor1  negative2 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(-2, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(-2, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, false, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, false, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-2, false, 0x7fffff)).normal() == 0x017f0000);
}

TEST_CASE("compact  constructor1  negative1 exponent normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(-1, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-1, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-1, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-1, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(-1, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(-1, false, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(-1, false, 0xffff)).normal() == 0x0200ff00);
    REQUIRE(compact(factory(-1, false, 0x7fffff)).normal() == 0x027fff00);
}

TEST_CASE("compact  constructor1  zero exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(0, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(0, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(0, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(0, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(0, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(0, false, 0xff)).normal() == 0x0200ff00);
    REQUIRE(compact(factory(0, false, 0xffff)).normal() == 0x0300ffff);
    REQUIRE(compact(factory(0, false, 0x7fffff)).normal() == 0x037fffff);
}

TEST_CASE("compact  constructor1  positive1 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(1, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(1, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(1, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(1, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(1, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(1, false, 0xff)).normal() == 0x0300ff00);
    REQUIRE(compact(factory(1, false, 0xffff)).normal() == 0x0400ffff);
    REQUIRE(compact(factory(1, false, 0x7fffff)).normal() == 0x047fffff);
}

TEST_CASE("compact  constructor1  positive29 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(29, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(29, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(29, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(29, true, 0x7fffff)).normal() == 0x00000000);

    // positive
    REQUIRE(compact(factory(29, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(29, false, 0xff)).normal() == 0x1f00ff00);
    REQUIRE(compact(factory(29, false, 0xffff)).normal() == 0x2000ffff);
    REQUIRE(compact(factory(29, false, 0x7fffff)).normal() == 0x207fffff);
}

TEST_CASE("compact  constructor1  positive30 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(30, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(30, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(30, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(30, true, 0x7fffff)).normal() == 0x00000000);

    // positive, overflow above 0xffff
    REQUIRE(compact(factory(30, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(30, false, 0xff)).normal() == 0x2000ff00);
    REQUIRE(compact(factory(30, false, 0xffff)).normal() == 0x2100ffff);
    REQUIRE(compact(factory(30, false, 0x7fffff)).normal() == 0x00000000);
}

TEST_CASE("compact  constructor1  positive31 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(31, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(31, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(31, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(31, true, 0x7fffff)).normal() == 0x00000000);

    // positive, overflow above 0xff
    REQUIRE(compact(factory(31, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(31, false, 0xff)).normal() == 0x2100ff00);
    REQUIRE(compact(factory(31, false, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(31, false, 0x7fffff)).normal() == 0x00000000);
}

TEST_CASE("compact  constructor1  positive32 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(32, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, true, 0x7fffff)).normal() == 0x00000000);

    // positive, always overflow
    REQUIRE(compact(factory(32, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, false, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, false, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(32, false, 0x7fffff)).normal() == 0x00000000);
}

TEST_CASE("compact  constructor1  positive252 exponent  normalizes as expected", "[compact]") {
    // negative, always zero
    REQUIRE(compact(factory(252, true, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, true, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, true, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, true, 0x7fffff)).normal() == 0x00000000);

    // positive, always overflow
    REQUIRE(compact(factory(252, false, 0)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, false, 0xff)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, false, 0xffff)).normal() == 0x00000000);
    REQUIRE(compact(factory(252, false, 0x7fffff)).normal() == 0x00000000);
}

// constructor2/uint256_t

TEST_CASE("compact  constructor2  zero  round trips", "[compact]") {
    REQUIRE(uint256_t(0) == compact(uint256_t(0)));
}

TEST_CASE("compact  constructor2  big value  round trips", "[compact]") {
    REQUIRE(uint256_t(42) == compact(uint256_t(42)));
}

TEST_CASE("compact  constructor2  hash  round trips", "[compact]") {
    REQUIRE(to_uint256(primes) == compact(to_uint256(primes)));
}

// End Test Suite
