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

// Normalized value of a valid (non-overflowing) compact encoding. Fails the
// test if the encoding overflows. Negative and zero-mantissa encodings are
// consensus-floored to zero and are *valid* (they normalize to 0), distinct
// from an overflow.
static uint32_t normal_of(uint32_t bits) {
    auto const c = compact::from_compact(bits);
    REQUIRE(c);
    return c->normal();
}

// True if the 32-bit compact encoding overflows 256 bits (an invalid target).
static bool overflows(uint32_t bits) {
    return ! compact::from_compact(bits);
}

TEST_CASE("compact from_compact proof of work limit normalizes unchanged", "[compact]") {
    REQUIRE(normal_of(retarget_proof_of_work_limit) == retarget_proof_of_work_limit);
}

TEST_CASE("compact from_compact no retarget proof of work limit normalizes unchanged", "[compact]") {
    REQUIRE(normal_of(no_retarget_proof_of_work_limit) == no_retarget_proof_of_work_limit);
}

// from_compact/normal

TEST_CASE("compact from_compact negative3 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(-3, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(-3, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, false, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, false, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(-3, false, 0x7fffff)) == 0x00000000);
}

TEST_CASE("compact from_compact negative2 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(-2, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(-2, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, false, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, false, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(-2, false, 0x7fffff)) == 0x017f0000);
}

TEST_CASE("compact from_compact negative1 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(-1, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-1, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-1, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(-1, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(-1, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(-1, false, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(-1, false, 0xffff)) == 0x0200ff00);
    REQUIRE(normal_of(factory(-1, false, 0x7fffff)) == 0x027fff00);
}

TEST_CASE("compact from_compact zero exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(0, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(0, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(0, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(0, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(0, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(0, false, 0xff)) == 0x0200ff00);
    REQUIRE(normal_of(factory(0, false, 0xffff)) == 0x0300ffff);
    REQUIRE(normal_of(factory(0, false, 0x7fffff)) == 0x037fffff);
}

TEST_CASE("compact from_compact positive1 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(1, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(1, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(1, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(1, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(1, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(1, false, 0xff)) == 0x0300ff00);
    REQUIRE(normal_of(factory(1, false, 0xffff)) == 0x0400ffff);
    REQUIRE(normal_of(factory(1, false, 0x7fffff)) == 0x047fffff);
}

TEST_CASE("compact from_compact positive29 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(29, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(29, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(29, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(29, true, 0x7fffff)) == 0x00000000);

    // positive
    REQUIRE(normal_of(factory(29, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(29, false, 0xff)) == 0x1f00ff00);
    REQUIRE(normal_of(factory(29, false, 0xffff)) == 0x2000ffff);
    REQUIRE(normal_of(factory(29, false, 0x7fffff)) == 0x207fffff);
}

TEST_CASE("compact from_compact positive30 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(30, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(30, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(30, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(30, true, 0x7fffff)) == 0x00000000);

    // positive; zero mantissa floors to zero, largest mantissa overflows
    REQUIRE(normal_of(factory(30, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(30, false, 0xff)) == 0x2000ff00);
    REQUIRE(normal_of(factory(30, false, 0xffff)) == 0x2100ffff);
    REQUIRE(overflows(factory(30, false, 0x7fffff)));
}

TEST_CASE("compact from_compact positive31 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(31, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(31, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(31, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(31, true, 0x7fffff)) == 0x00000000);

    // positive; zero mantissa floors to zero, larger mantissas overflow
    REQUIRE(normal_of(factory(31, false, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(31, false, 0xff)) == 0x2100ff00);
    REQUIRE(overflows(factory(31, false, 0xffff)));
    REQUIRE(overflows(factory(31, false, 0x7fffff)));
}

TEST_CASE("compact from_compact positive32 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(32, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(32, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(32, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(32, true, 0x7fffff)) == 0x00000000);

    // positive; zero mantissa floors to zero, any non-zero mantissa overflows
    REQUIRE(normal_of(factory(32, false, 0)) == 0x00000000);
    REQUIRE(overflows(factory(32, false, 0xff)));
    REQUIRE(overflows(factory(32, false, 0xffff)));
    REQUIRE(overflows(factory(32, false, 0x7fffff)));
}

TEST_CASE("compact from_compact positive252 exponent normalizes as expected", "[compact]") {
    // negative, always floored to zero (valid, not overflow)
    REQUIRE(normal_of(factory(252, true, 0)) == 0x00000000);
    REQUIRE(normal_of(factory(252, true, 0xff)) == 0x00000000);
    REQUIRE(normal_of(factory(252, true, 0xffff)) == 0x00000000);
    REQUIRE(normal_of(factory(252, true, 0x7fffff)) == 0x00000000);

    // positive; zero mantissa floors to zero, any non-zero mantissa overflows
    REQUIRE(normal_of(factory(252, false, 0)) == 0x00000000);
    REQUIRE(overflows(factory(252, false, 0xff)));
    REQUIRE(overflows(factory(252, false, 0xffff)));
    REQUIRE(overflows(factory(252, false, 0x7fffff)));
}

// constructor uint256_t

TEST_CASE("compact constructor uint256 zero round trips", "[compact]") {
    REQUIRE(uint256_t(0) == compact(uint256_t(0)));
}

TEST_CASE("compact constructor uint256 big value round trips", "[compact]") {
    REQUIRE(uint256_t(42) == compact(uint256_t(42)));
}

TEST_CASE("compact constructor uint256 hash round trips", "[compact]") {
    REQUIRE(to_uint256(primes) == compact(to_uint256(primes)));
}

// End Test Suite
