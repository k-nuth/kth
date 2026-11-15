// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Scalar arithmetic mod the group order n (plain 4x64 limbs, Montgomery
// multiplication). This is per-signature preprocessing that runs on the
// scalar side before the lane-parallel kernel.

#ifndef KTH_SECP256K1_TURBO_SCALAR256_HPP
#define KTH_SECP256K1_TURBO_SCALAR256_HPP

#include <cstdint>
#include <cstring>

#include "u128.hpp"

namespace secp256k1_turbo {

struct u256 { std::uint64_t d[4]; };  // little-endian limbs

namespace sn {

constexpr u256 N = {{0xBFD25E8CD0364141ULL, 0xBAAEDCE6AF48A03BULL, 0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL}};
// (n - 1) / 2: s is "high" (non-canonical ECDSA) when s > N_HALF.
constexpr u256 N_HALF = {{0xDFE92F46681B20A0ULL, 0x5D576E7357A4501DULL, 0xFFFFFFFFFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL}};
constexpr u256 ZERO = {{0, 0, 0, 0}};
constexpr u256 ONE = {{1, 0, 0, 0}};

// -n^-1 mod 2^64 (Newton iteration, each step doubles the correct bits).
constexpr std::uint64_t n_inv_neg() {
    std::uint64_t inv = 1;
    for (int i = 0; i < 7; ++i) inv *= 2 - N.d[0] * inv;
    return 0 - inv;
}
constexpr std::uint64_t N_INV_NEG = n_inv_neg();

constexpr bool lt(u256 const& a, u256 const& b) {
    for (int i = 3; i >= 0; --i) {
        if (a.d[i] < b.d[i]) return true;
        if (a.d[i] > b.d[i]) return false;
    }
    return false;
}
constexpr bool is_zero(u256 const& a) { return (a.d[0] | a.d[1] | a.d[2] | a.d[3]) == 0; }
constexpr bool eq(u256 const& a, u256 const& b) { return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3]; }

// r = a + b (mod 2^256), returns the carry.
constexpr std::uint64_t add_raw(u256& r, u256 const& a, u256 const& b) {
    std::uint64_t c = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint64_t const s = a.d[i] + c;
        std::uint64_t c1 = s < c;
        std::uint64_t const t = s + b.d[i];
        c1 += t < s;
        r.d[i] = t; c = c1;
    }
    return c;
}
// r = a - b (mod 2^256), returns the borrow.
constexpr std::uint64_t sub_raw(u256& r, u256 const& a, u256 const& b) {
    std::uint64_t br = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint64_t const s = a.d[i] - br;
        std::uint64_t b1 = a.d[i] < br;
        std::uint64_t const t = s - b.d[i];
        b1 += s < b.d[i];
        r.d[i] = t; br = b1;
    }
    return br;
}

constexpr u256 add_mod(u256 const& a, u256 const& b) {
    u256 r{};
    std::uint64_t const c = add_raw(r, a, b);
    if (c || !lt(r, N)) { u256 t{}; sub_raw(t, r, N); return t; }
    return r;
}
constexpr u256 sub_mod(u256 const& a, u256 const& b) {
    u256 r{};
    if (sub_raw(r, a, b)) { u256 t{}; add_raw(t, r, N); return t; }
    return r;
}
constexpr u256 neg_mod(u256 const& a) { return is_zero(a) ? a : sub_mod(ZERO, a); }

// 2^512 mod n, computed by repeated doubling.
constexpr u256 r2_mod_n() {
    u256 x = ONE;
    for (int i = 0; i < 512; ++i) x = add_mod(x, x);
    return x;
}
constexpr u256 R2 = r2_mod_n();

// Montgomery product a * b * 2^-256 mod n (CIOS).
inline u256 mont_mul(u256 const& a, u256 const& b) {
    std::uint64_t t[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        std::uint64_t c = 0;
        for (int j = 0; j < 4; ++j) {
            u128 const p = mul64(a.d[j], b.d[i]);
            std::uint64_t cc = 0;
            std::uint64_t lo = addc(p.lo, t[j], cc);
            std::uint64_t hi = p.hi + cc;
            cc = 0;
            lo = addc(lo, c, cc);
            hi += cc;
            t[j] = lo; c = hi;
        }
        std::uint64_t cc = 0;
        t[4] = addc(t[4], c, cc);
        t[5] = cc;
        std::uint64_t const m = t[0] * N_INV_NEG;
        {
            u128 const p = mul64(m, N.d[0]);
            cc = 0;
            (void)addc(p.lo, t[0], cc);
            c = p.hi + cc;
        }
        for (int j = 1; j < 4; ++j) {
            u128 const p = mul64(m, N.d[j]);
            cc = 0;
            std::uint64_t lo = addc(p.lo, t[j], cc);
            std::uint64_t hi = p.hi + cc;
            cc = 0;
            lo = addc(lo, c, cc);
            hi += cc;
            t[j - 1] = lo; c = hi;
        }
        cc = 0;
        t[3] = addc(t[4], c, cc);
        t[4] = t[5] + cc;
    }
    u256 r = {{t[0], t[1], t[2], t[3]}};
    if (t[4] || !lt(r, N)) { u256 s{}; sub_raw(s, r, N); return s; }
    return r;
}

inline u256 to_mont(u256 const& a) { return mont_mul(a, R2); }
inline u256 from_mont(u256 const& a) { return mont_mul(a, ONE); }
// Plain modular product (not in Montgomery form).
inline u256 mul_mod(u256 const& a, u256 const& b) { return mont_mul(mont_mul(a, b), R2); }

// a^(n-2) in Montgomery form (input and output in Montgomery form).
inline u256 inverse_mont(u256 const& a) {
    u256 e{}; sub_raw(e, N, {{2, 0, 0, 0}});
    u256 const one_m = to_mont(ONE);
    u256 r = one_m;
    for (int i = 255; i >= 0; --i) {
        r = mont_mul(r, r);
        if ((e.d[i / 64] >> (i % 64)) & 1) r = mont_mul(r, a);
    }
    return r;
}

// Big-endian bytes -> scalar; returns false (and reduces) if >= n.
inline bool from_be32(std::uint8_t const* b, u256& out) {
    for (int i = 0; i < 4; ++i) {
        std::uint64_t x = 0;
        for (int j = 0; j < 8; ++j) x = (x << 8) | b[i * 8 + j];
        out.d[3 - i] = x;
    }
    if (!lt(out, N)) { u256 t{}; sub_raw(t, out, N); out = t; return false; }
    return true;
}
inline void to_be32(u256 const& a, std::uint8_t* b) {
    for (int i = 0; i < 4; ++i) {
        std::uint64_t x = a.d[3 - i];
        for (int j = 7; j >= 0; --j) { b[i * 8 + j] = static_cast<std::uint8_t>(x); x >>= 8; }
    }
}

// 512-bit product.
inline void mul_512(std::uint64_t l[8], u256 const& a, u256 const& b) {
    for (int i = 0; i < 8; ++i) l[i] = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint64_t c = 0;
        for (int j = 0; j < 4; ++j) {
            u128 const p = mul64(a.d[i], b.d[j]);
            std::uint64_t cc = 0;
            std::uint64_t lo = addc(p.lo, l[i + j], cc);
            std::uint64_t hi = p.hi + cc;
            cc = 0;
            lo = addc(lo, c, cc);
            hi += cc;
            l[i + j] = lo; c = hi;
        }
        l[i + 4] = c;
    }
}

// round((a * b) / 2^384), as in secp256k1_scalar_mul_shift_var(..., 384).
inline u256 mul_shift_384(u256 const& a, u256 const& b) {
    std::uint64_t l[8];
    mul_512(l, a, b);
    u256 r = {{l[6], l[7], 0, 0}};
    u256 const bit = {{(l[5] >> 63) & 1, 0, 0, 0}};
    u256 s{}; add_raw(s, r, bit);
    return s;
}

// GLV constants (secp256k1_scalar_split_lambda).
constexpr u256 LAMBDA   = {{0xDF02967C1B23BD72ULL, 0x122E22EA20816678ULL, 0xA5261C028812645AULL, 0x5363AD4CC05C30E0ULL}};
constexpr u256 MINUS_B1 = {{0x6F547FA90ABFE4C3ULL, 0xE4437ED6010E8828ULL, 0, 0}};
constexpr u256 MINUS_B2 = {{0xD765CDA83DB1562CULL, 0x8A280AC50774346DULL, 0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL}};
constexpr u256 G1       = {{0xE893209A45DBB031ULL, 0x3DAA8A1471E8CA7FULL, 0xE86C90E49284EB15ULL, 0x3086D221A7D46BCDULL}};
constexpr u256 G2       = {{0x1571B4AE8AC47F71ULL, 0x221208AC9DF506C6ULL, 0x6F547FA90ABFE4C4ULL, 0xE4437ED6010E8828ULL}};

struct split_result {
    u256 k1, k2;        // magnitudes, both < 2^128
    bool k1neg, k2neg;  // k == (+-k1) + lambda * (+-k2) mod n
};

// Decomposes k into k1 + lambda * k2 with |k1|, |k2| < 2^128.
inline split_result split_lambda(u256 const& k) {
    u256 c1 = mul_shift_384(k, G1);
    u256 c2 = mul_shift_384(k, G2);
    c1 = mul_mod(c1, MINUS_B1);
    c2 = mul_mod(c2, MINUS_B2);
    u256 const r2 = add_mod(c1, c2);
    u256 r1 = mul_mod(r2, LAMBDA);
    r1 = neg_mod(r1);
    r1 = add_mod(r1, k);
    split_result s;
    s.k1neg = (r1.d[2] | r1.d[3]) != 0;
    s.k1 = s.k1neg ? neg_mod(r1) : r1;
    s.k2neg = (r2.d[2] | r2.d[3]) != 0;
    s.k2 = s.k2neg ? neg_mod(r2) : r2;
    return s;
}

// 4-bit digit j (little-endian) of a scalar.
inline std::uint64_t nibble(u256 const& a, int j) { return (a.d[j / 16] >> (4 * (j % 16))) & 15; }

} // namespace sn
} // namespace secp256k1_turbo

#endif
