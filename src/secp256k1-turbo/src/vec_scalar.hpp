// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Portable one-lane backend. It mirrors the AVX-512 IFMA semantics exactly
// (madd52 only reads the low 52 bits of its multiplicands) so that the two
// backends execute the same algorithm bit for bit; it also serves as the
// reference the vector kernels are tested against.

#ifndef KTH_SECP256K1_TURBO_VEC_SCALAR_HPP
#define KTH_SECP256K1_TURBO_VEC_SCALAR_HPP

#include <cstddef>
#include <cstdint>

#include "u128.hpp"

#ifdef KTH_SECP256K1_TURBO_CHECK_LIMBS
#include <cstdio>
#include <cstdlib>
// Not assert(): the tests that ask for this check are built in Release too,
// where NDEBUG would compile an assert away and leave the precondition
// unchecked exactly where it is meant to be exercised.
#define KTH_TURBO_CHECK52(x) \
    do { \
        if (((x) >> 52) != 0) { \
            std::fputs("madd52 operand exceeds 52 bits\n", stderr); \
            std::abort(); \
        } \
    } while (false)
#else
#define KTH_TURBO_CHECK52(x) ((void)0)
#endif

namespace secp256k1_turbo {

struct vec1 {
    static constexpr std::size_t lanes = 1;
    std::uint64_t v;

    static vec1 zero() { return {0}; }
    static vec1 set1(std::uint64_t x) { return {x}; }
    // Loads `lanes` consecutive u64 (lane-minor layout).
    static vec1 load(std::uint64_t const* p) { return {p[0]}; }
    void store(std::uint64_t* p) const { p[0] = v; }
    std::uint64_t lane(std::size_t) const { return v; }

    friend vec1 operator+(vec1 a, vec1 b) { return {a.v + b.v}; }
    friend vec1 operator-(vec1 a, vec1 b) { return {a.v - b.v}; }
    friend vec1 operator&(vec1 a, vec1 b) { return {a.v & b.v}; }
    friend vec1 operator|(vec1 a, vec1 b) { return {a.v | b.v}; }
    friend vec1 operator^(vec1 a, vec1 b) { return {a.v ^ b.v}; }
    friend vec1 operator~(vec1 a) { return {~a.v}; }
    template <int S> friend vec1 shr(vec1 a) { return {a.v >> S}; }
    template <int S> friend vec1 shl(vec1 a) { return {a.v << S}; }
    friend vec1 andnot(vec1 a, vec1 b) { return {~a.v & b.v}; }  // (~a) & b

    // acc + low/high 52 bits of (a[51:0] * b[51:0]).
    friend vec1 madd52lo(vec1 acc, vec1 a, vec1 b) {
        KTH_TURBO_CHECK52(a.v); KTH_TURBO_CHECK52(b.v);
        return {acc.v + static_cast<std::uint64_t>(mul64(a.v & mask52, b.v & mask52).lo & mask52)};
    }
    friend vec1 madd52hi(vec1 acc, vec1 a, vec1 b) {
        KTH_TURBO_CHECK52(a.v); KTH_TURBO_CHECK52(b.v);
        auto const p = mul64(a.v & mask52, b.v & mask52);
        return {acc.v + static_cast<std::uint64_t>((p.lo >> 52) | (p.hi << 12))};
    }

    // Masks are plain vectors of 0 / ~0.
    friend vec1 cmpeq(vec1 a, vec1 b) { return {a.v == b.v ? ~std::uint64_t(0) : 0}; }
    friend vec1 cmpgt(vec1 a, vec1 b) { return {a.v > b.v ? ~std::uint64_t(0) : 0}; }  // unsigned
    friend vec1 blend(vec1 m, vec1 a, vec1 b) { return {(a.v & m.v) | (b.v & ~m.v)}; }  // m ? a : b

    // base[idx] with a per-lane index.
    friend vec1 gather(std::uint64_t const* base, vec1 idx) { return {base[idx.v]}; }

    static constexpr std::uint64_t mask52 = 0xFFFFFFFFFFFFFULL;
};

} // namespace secp256k1_turbo

#endif
