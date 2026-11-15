// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SECP256K1_TURBO_U128_HPP
#define KTH_SECP256K1_TURBO_U128_HPP

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace secp256k1_turbo {

struct u128 { std::uint64_t lo, hi; };

inline u128 mul64(std::uint64_t a, std::uint64_t b) {
#if defined(_MSC_VER) && !defined(__clang__)
    u128 r;
    r.lo = _umul128(a, b, &r.hi);
    return r;
#else
    unsigned __int128 const p = static_cast<unsigned __int128>(a) * b;
    return {static_cast<std::uint64_t>(p), static_cast<std::uint64_t>(p >> 64)};
#endif
}

// a + b + carry_in -> (sum, carry_out)
inline std::uint64_t addc(std::uint64_t a, std::uint64_t b, std::uint64_t& carry) {
    std::uint64_t const s = a + b;
    std::uint64_t c = s < a;
    std::uint64_t const t = s + carry;
    c += t < s;
    carry = c;
    return t;
}

// a - b - borrow_in -> (diff, borrow_out)
inline std::uint64_t subb(std::uint64_t a, std::uint64_t b, std::uint64_t& borrow) {
    std::uint64_t const d = a - b;
    std::uint64_t br = a < b;
    std::uint64_t const t = d - borrow;
    br += d < borrow;
    borrow = br;
    return t;
}

} // namespace secp256k1_turbo

#endif
