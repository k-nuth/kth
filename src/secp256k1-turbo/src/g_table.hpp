// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SECP256K1_TURBO_G_TABLE_HPP
#define KTH_SECP256K1_TURBO_G_TABLE_HPP

#include <cstdint>

namespace secp256k1_turbo {

// Two-tooth comb table for G, aligned with the 4-bit doubling ladder of the
// kernel: entry d = c + 16 * c' (c, c' in 0..15) holds the affine point
// c * G + c' * 2^128 * G as 10 limbs (x0..x4, y0..y4 in radix 2^52). At ladder
// step j the kernel adds entry (nibble j of u1, nibble j + 32 of u1), so one
// addition per step covers all 256 bits of the G scalar. Entry 0 is unused.
struct g_table {
    static constexpr int entries = 256;
    static constexpr int limbs = 10;
    std::uint64_t t[entries][limbs];
};

g_table const& get_g_table();

// Generator in 5x52 limbs.
extern std::uint64_t const G_X[5];
extern std::uint64_t const G_Y[5];

} // namespace secp256k1_turbo

#endif
