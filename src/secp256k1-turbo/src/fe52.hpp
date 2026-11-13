// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Field arithmetic mod p = 2^256 - 2^32 - 977 in radix 2^52 (5 limbs), one
// field element per SIMD lane. The representation invariant is tighter than
// libsecp256k1's: every limb of every input to mul/sqr is strictly below
// 2^52, because the IFMA instructions ignore the upper 12 bits of their
// multiplicands. Every operation therefore ends with norm52(), which brings
// all limbs below 2^52 again (the value itself stays non-canonical, below
// 2^260, until normalize() is called).

#ifndef KTH_SECP256K1_TURBO_FE52_HPP
#define KTH_SECP256K1_TURBO_FE52_HPP

#include <cstddef>
#include <cstdint>

namespace secp256k1_turbo {

namespace fe_const {
constexpr std::uint64_t M52 = 0xFFFFFFFFFFFFFULL;
constexpr std::uint64_t M48 = 0x0FFFFFFFFFFFFULL;
// 2^256 mod p and 2^260 mod p.
constexpr std::uint64_t R1 = 0x1000003D1ULL;
constexpr std::uint64_t R  = 0x1000003D10ULL;
// 2^520 mod p == R*R, split at 52 bits (R*R < 2^74).
constexpr std::uint64_t R2_LO = (0x3D10ULL << 37) + 0x3D10ULL * 0x3D10ULL;
constexpr std::uint64_t R2_HI = 1ULL << 20;
// p in 5x52 limbs, and a multiple of p used by sub/neg to keep limbs non-negative.
constexpr std::uint64_t P0 = 0xFFFFEFFFFFC2FULL;
constexpr std::uint64_t P1 = 0xFFFFFFFFFFFFFULL;
constexpr std::uint64_t P4 = 0x0FFFFFFFFFFFFULL;
// 32p, limb-wise: large enough that a + 32p - b never goes negative for any
// limbs below 2^52 (the top limb of p only has 48 bits).
constexpr std::uint64_t KP0 = 32 * P0;
constexpr std::uint64_t KP1 = 32 * P1;
constexpr std::uint64_t KP4 = 32 * P4;

// Exponents as 4 little-endian 64-bit limbs.
constexpr std::uint64_t P_MINUS_2[4]   = {0xFFFFFFFEFFFFFC2DULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
constexpr std::uint64_t P_PLUS_1_DIV_4[4] = {0xFFFFFFFFBFFFFF0CULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x3FFFFFFFFFFFFFFFULL};
constexpr std::uint64_t P_MINUS_1_DIV_2[4] = {0xFFFFFFFF7FFFFE17ULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL};

// beta: cube root of unity in the field (the GLV endomorphism), big-endian.
constexpr std::uint8_t BETA_BE[32] = {
    0x7a,0xe9,0x6a,0x2b,0x65,0x7c,0x07,0x10,0x6e,0x64,0x47,0x9e,0xac,0x34,0x34,0xe9,
    0x9c,0xf0,0x49,0x75,0x12,0xf5,0x89,0x95,0xc1,0x39,0x6c,0x28,0x71,0x95,0x01,0xee};
} // namespace fe_const

template <class V>
struct fe {
    V n[5];
};

// Big-endian 32 bytes -> 5x52 limbs (no reduction; caller checks < p when it matters).
inline void fe_limbs_from_be32(std::uint8_t const* b, std::uint64_t out[5]) {
    std::uint64_t w[4];
    for (int i = 0; i < 4; ++i) {
        std::uint64_t x = 0;
        for (int j = 0; j < 8; ++j) x = (x << 8) | b[i * 8 + j];
        w[3 - i] = x;  // w[0] is the least significant word
    }
    out[0] = w[0] & fe_const::M52;
    out[1] = ((w[0] >> 52) | (w[1] << 12)) & fe_const::M52;
    out[2] = ((w[1] >> 40) | (w[2] << 24)) & fe_const::M52;
    out[3] = ((w[2] >> 28) | (w[3] << 36)) & fe_const::M52;
    out[4] = w[3] >> 16;
}

// Canonical 5x52 limbs -> big-endian 32 bytes.
inline void fe_limbs_to_be32(std::uint64_t const n[5], std::uint8_t* b) {
    std::uint64_t w[4];
    w[0] = n[0] | (n[1] << 52);
    w[1] = (n[1] >> 12) | (n[2] << 40);
    w[2] = (n[2] >> 24) | (n[3] << 28);
    w[3] = (n[3] >> 36) | (n[4] << 16);
    for (int i = 0; i < 4; ++i) {
        std::uint64_t x = w[3 - i];
        for (int j = 7; j >= 0; --j) { b[i * 8 + j] = static_cast<std::uint8_t>(x); x >>= 8; }
    }
}

// Lexicographic (big-endian) comparison of 32 bytes against p: returns true if < p.
inline bool be32_less_than_p(std::uint8_t const* b) {
    static constexpr std::uint8_t P[32] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFC,0x2F};
    for (int i = 0; i < 32; ++i) {
        if (b[i] < P[i]) return true;
        if (b[i] > P[i]) return false;
    }
    return false;
}

template <class V>
struct field {
    using F = fe<V>;

    static F zero() { F r; for (auto& l : r.n) l = V::zero(); return r; }
    static F one() { F r = zero(); r.n[0] = V::set1(1); return r; }
    static F from_limbs(std::uint64_t const l[5]) { F r; for (int i = 0; i < 5; ++i) r.n[i] = V::set1(l[i]); return r; }
    static F beta() { std::uint64_t l[5]; fe_limbs_from_be32(fe_const::BETA_BE, l); return from_limbs(l); }

    // Loads limb i of the lanes from a limb-major array: p[i * stride ... ].
    static F load(std::uint64_t const* p, std::size_t stride) {
        F r; for (int i = 0; i < 5; ++i) r.n[i] = V::load(p + i * stride); return r;
    }
    static void store(F const& a, std::uint64_t* p, std::size_t stride) {
        for (int i = 0; i < 5; ++i) a.n[i].store(p + i * stride);
    }

    static F select(V m, F const& a, F const& b) {
        F r; for (int i = 0; i < 5; ++i) r.n[i] = blend(m, a.n[i], b.n[i]); return r;
    }

    // Brings every limb below 2^52 (input limbs may be as large as 2^63).
    static void norm52(F& a) {
        using namespace fe_const;
        V const m52 = V::set1(M52);
        V x = shr<48>(a.n[4]);
        a.n[4] = a.n[4] & V::set1(M48);
        a.n[0] = madd52lo(a.n[0], x, V::set1(R1));
        a.n[1] = a.n[1] + shr<52>(a.n[0]); a.n[0] = a.n[0] & m52;
        a.n[2] = a.n[2] + shr<52>(a.n[1]); a.n[1] = a.n[1] & m52;
        a.n[3] = a.n[3] + shr<52>(a.n[2]); a.n[2] = a.n[2] & m52;
        a.n[4] = a.n[4] + shr<52>(a.n[3]); a.n[3] = a.n[3] & m52;
    }

    // Fully reduces to the canonical representative in [0, p).
    static void normalize(F& a) {
        using namespace fe_const;
        norm52(a);
        V const m52 = V::set1(M52);
        V const m = a.n[1] & a.n[2] & a.n[3];
        V const ge_p = cmpeq(a.n[4], V::set1(M48)) & cmpeq(m, m52) & ~cmpgt(V::set1(P0), a.n[0]);
        V const x = shr<48>(a.n[4]) | (ge_p & V::set1(1));
        a.n[0] = madd52lo(a.n[0], x, V::set1(R1));
        a.n[1] = a.n[1] + shr<52>(a.n[0]); a.n[0] = a.n[0] & m52;
        a.n[2] = a.n[2] + shr<52>(a.n[1]); a.n[1] = a.n[1] & m52;
        a.n[3] = a.n[3] + shr<52>(a.n[2]); a.n[2] = a.n[2] & m52;
        a.n[4] = a.n[4] + shr<52>(a.n[3]); a.n[3] = a.n[3] & m52;
        a.n[4] = a.n[4] & V::set1(M48);
    }

    static F add(F const& a, F const& b) {
        F r; for (int i = 0; i < 5; ++i) r.n[i] = a.n[i] + b.n[i];
        norm52(r); return r;
    }
    // a - b computed as a + 32p - b; limbs stay non-negative.
    static F sub(F const& a, F const& b) {
        using namespace fe_const;
        F r;
        r.n[0] = a.n[0] + V::set1(KP0) - b.n[0];
        r.n[1] = a.n[1] + V::set1(KP1) - b.n[1];
        r.n[2] = a.n[2] + V::set1(KP1) - b.n[2];
        r.n[3] = a.n[3] + V::set1(KP1) - b.n[3];
        r.n[4] = a.n[4] + V::set1(KP4) - b.n[4];
        norm52(r); return r;
    }
    static F neg(F const& a) { return sub(zero(), a); }
    static F cneg(V m, F const& a) { return select(m, neg(a), a); }

    // a * k for a small constant k (k <= 8).
    template <int K>
    static F mul_int(F const& a) {
        static_assert(K >= 2 && K <= 8);
        F r;
        for (int i = 0; i < 5; ++i) {
            V x = a.n[i];
            if constexpr (K == 2) x = shl<1>(x);
            else if constexpr (K == 3) x = shl<1>(x) + x;
            else if constexpr (K == 4) x = shl<2>(x);
            else if constexpr (K == 8) x = shl<3>(x);
            else { V y = x; for (int k = 1; k < K; ++k) y = y + x; x = y; }
            r.n[i] = x;
        }
        norm52(r); return r;
    }

    // a / 2 mod p.
    static F half(F const& a) {
        using namespace fe_const;
        V const odd = V::zero() - (a.n[0] & V::set1(1));  // all ones if odd
        F t;
        t.n[0] = a.n[0] + (odd & V::set1(P0));
        t.n[1] = a.n[1] + (odd & V::set1(P1));
        t.n[2] = a.n[2] + (odd & V::set1(P1));
        t.n[3] = a.n[3] + (odd & V::set1(P1));
        t.n[4] = a.n[4] + (odd & V::set1(P4));
        V const one = V::set1(1);
        F r;
        // t limbs may exceed 2^52 (a + p), so the shifted-in bit is added, not or-ed.
        r.n[0] = shr<1>(t.n[0]) + shl<51>(t.n[1] & one);
        r.n[1] = shr<1>(t.n[1]) + shl<51>(t.n[2] & one);
        r.n[2] = shr<1>(t.n[2]) + shl<51>(t.n[3] & one);
        r.n[3] = shr<1>(t.n[3]) + shl<51>(t.n[4] & one);
        r.n[4] = shr<1>(t.n[4]);
        norm52(r); return r;
    }

    // Reduces 11 product columns (c[10] is the carry out of c[9]) into a.
    static void reduce_columns(V c[11], F& r) {
        using namespace fe_const;
        V const m52 = V::set1(M52);
        for (int k = 5; k < 10; ++k) {
            c[k + 1] = c[k + 1] + shr<52>(c[k]);
            c[k] = c[k] & m52;
        }
        V const rr = V::set1(R);
        c[0] = madd52lo(c[0], c[10], V::set1(R2_LO));
        c[1] = madd52hi(c[1], c[10], V::set1(R2_LO));
        c[1] = madd52lo(c[1], c[10], V::set1(R2_HI));
        for (int k = 5; k < 9; ++k) {
            c[k - 5] = madd52lo(c[k - 5], c[k], rr);
            c[k - 4] = madd52hi(c[k - 4], c[k], rr);
        }
        // The high half of c[9] * R lands in column 5 again (< 2^37): fold it once more.
        c[4] = madd52lo(c[4], c[9], rr);
        V const t = madd52hi(V::zero(), c[9], rr);
        c[0] = madd52lo(c[0], t, rr);
        c[1] = madd52hi(c[1], t, rr);
        for (int i = 0; i < 5; ++i) r.n[i] = c[i];
        norm52(r);
    }

    static F mul(F const& a, F const& b) {
        V c[11];
        for (auto& x : c) x = V::zero();
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                c[i + j] = madd52lo(c[i + j], a.n[i], b.n[j]);
                c[i + j + 1] = madd52hi(c[i + j + 1], a.n[i], b.n[j]);
            }
        }
        F r; reduce_columns(c, r); return r;
    }

    static F sqr(F const& a) {
        V d[11], x[11];
        for (int k = 0; k < 11; ++k) { d[k] = V::zero(); x[k] = V::zero(); }
        for (int i = 0; i < 5; ++i) {
            d[2 * i] = madd52lo(d[2 * i], a.n[i], a.n[i]);
            d[2 * i + 1] = madd52hi(d[2 * i + 1], a.n[i], a.n[i]);
            for (int j = i + 1; j < 5; ++j) {
                x[i + j] = madd52lo(x[i + j], a.n[i], a.n[j]);
                x[i + j + 1] = madd52hi(x[i + j + 1], a.n[i], a.n[j]);
            }
        }
        for (int k = 0; k < 11; ++k) d[k] = d[k] + shl<1>(x[k]);
        F r; reduce_columns(d, r); return r;
    }

    // Mask: all ones where a == 0 mod p.
    static V is_zero(F const& a) {
        F t = a; normalize(t);
        return cmpeq(t.n[0] | t.n[1] | t.n[2] | t.n[3] | t.n[4], V::zero());
    }
    static V eq(F const& a, F const& b) { return is_zero(sub(a, b)); }

    // Mask: all ones where the canonical value is odd.
    static V is_odd(F const& a) {
        F t = a; normalize(t);
        return V::zero() - (t.n[0] & V::set1(1));
    }

    // a^e for a fixed exponent (4 little-endian 64-bit limbs), 4-bit windows.
    static F pow(F const& a, std::uint64_t const e[4]) {
        F tab[16];
        tab[0] = one(); tab[1] = a;
        for (int i = 2; i < 16; ++i) tab[i] = mul(tab[i - 1], a);
        F r = one();
        bool started = false;
        for (int w = 63; w >= 0; --w) {
            unsigned const nib = static_cast<unsigned>((e[w / 16] >> (4 * (w % 16))) & 15);
            if (started) { r = sqr(r); r = sqr(r); r = sqr(r); r = sqr(r); }
            if (nib != 0) {
                r = started ? mul(r, tab[nib]) : tab[nib];
                started = true;
            }
        }
        return r;
    }

    static F inverse(F const& a) { return pow(a, fe_const::P_MINUS_2); }
    // Candidate sqrt; caller checks sqr(result) == a. The result is itself a square.
    static F sqrt_candidate(F const& a) { return pow(a, fe_const::P_PLUS_1_DIV_4); }
    // Mask: all ones where a is a non-zero quadratic residue (Euler's criterion).
    static V is_square_nonzero(F const& a) { return eq(pow(a, fe_const::P_MINUS_1_DIV_2), one()); }
};

} // namespace secp256k1_turbo

#endif
