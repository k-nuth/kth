// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// arith_uint256_tests.cpp bignum_SetCompact of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): decoding of the
// compact target (nBits) of a block header, the value consensus compares the proof of work against.
// Original: Copyright (c) 2011-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// BCHN's SetCompact returns the magnitude plus a negative and an overflow flag, and CheckProofOfWork refuses a header with either flag.
// Knuth's compact::from_compact has no flags: an overflowing encoding is an error and a negative one is floored to a zero target, which no
// hash satisfies, so the outcome of the proof-of-work check is the same; the magnitude BCHN keeps for negative encodings is not
// reproduced and is listed below as such. GetCompact(true) of the original (re-encoding with the negative bit) has no counterpart.

#include <test_helpers.hpp>

#include <kth/domain/chain/compact.hpp>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

struct row {
    uint32_t bits;
    char const* hex;            // the 256-bit value, without leading zeros
    uint32_t renormalized;      // GetCompact() of the value (0 when the value is zero)
};

constexpr row rows[] = {
    {0x00000000, "0", 0x0u},
    {0x00123456, "0", 0x0u},
    {0x01003456, "0", 0x0u},
    {0x02000056, "0", 0x0u},
    {0x03000000, "0", 0x0u},
    {0x04000000, "0", 0x0u},
    {0x00923456, "0", 0x0u},
    {0x01803456, "0", 0x0u},
    {0x02800056, "0", 0x0u},
    {0x03800000, "0", 0x0u},
    {0x04800000, "0", 0x0u},
    {0x01123456, "12", 0x01120000u},
    {0x02123456, "1234", 0x02123400u},
    {0x03123456, "123456", 0x03123456u},
    {0x04123456, "12345600", 0x04123456u},
    {0x05009234, "92340000", 0x05009234u},
    {0x20123456, "1234560000000000000000000000000000000000000000000000000000000000", 0x20123456u},
};

constexpr struct { uint32_t bits; char const* magnitude; } negatives[] = {
    {0x01fedcba, "7e"},
    {0x04923456, "12345600"},
};

} // namespace

TEST_CASE("BCHN arith_uint256_tests - bignum_SetCompact", "[bchn_non_vm][compact]") {
    for (auto const& r : rows) {
        auto const parsed = chain::compact::from_compact(r.bits);
        REQUIRE(parsed);
        CHECK(parsed->big() == uint256_t(std::string("0x") + r.hex));
        CHECK(parsed->normal() == r.renormalized);
    }

    // 0x80 is encoded as 0x02008000: the 0x00800000 sign bit is never generated
    CHECK(chain::compact(uint256_t(0x80)).normal() == 0x02008000u);

    // negative encodings: floored to zero (BCHN: magnitude kept with fNegative, refused by CheckProofOfWork)
    for (auto const& n : negatives) {
        auto const parsed = chain::compact::from_compact(n.bits);
        REQUIRE(parsed);
        CHECK(parsed->big() == uint256_t(0));
    }

    // overflow: 0xff123456 shifts the mantissa past 256 bits
    CHECK( ! chain::compact::from_compact(0xff123456u));
}
