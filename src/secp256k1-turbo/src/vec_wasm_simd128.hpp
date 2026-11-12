// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Two-lane backend for WebAssembly SIMD128.
//
// Like NEON, SIMD128 has no madd52 and no widening 64x64 lane multiply; the
// widest lane product is 32x32 -> 64 (i64x2.extmul_low_i32x4_u). madd52lo and
// madd52hi are built from four 26x26 partial products, exactly as in
// vec_neon.hpp, where the arithmetic is spelled out. The same algorithm as
// the other backends, bit for bit.
//
// extmul reads its operands from the two low 32-bit lanes, and a 26-bit half
// of a 64-bit lane sits in 32-bit lanes 0 and 2, so each half is shuffled
// down first. i64x2.mul would take them where they are, but it is a full
// 64x64 multiply that engines lower to several instructions.
//
// best_backend() does not choose this backend: see the README for what it
// costs against scalar WebAssembly.

#ifndef KTH_SECP256K1_TURBO_VEC_WASM_SIMD128_HPP
#define KTH_SECP256K1_TURBO_VEC_WASM_SIMD128_HPP

#include <cstddef>
#include <cstdint>

#include <wasm_simd128.h>

namespace secp256k1_turbo {

struct vec2 {
    static constexpr std::size_t lanes = 2;
    v128_t v;

    static vec2 zero() { return {wasm_i64x2_const_splat(0)}; }
    static vec2 set1(std::uint64_t x) { return {wasm_i64x2_splat(static_cast<std::int64_t>(x))}; }
    static vec2 load(std::uint64_t const* p) { return {wasm_v128_load(p)}; }
    void store(std::uint64_t* p) const { wasm_v128_store(p, v); }
    std::uint64_t lane(std::size_t i) const {
        std::uint64_t t[2];
        wasm_v128_store(t, v);
        return t[i];
    }

    friend vec2 operator+(vec2 a, vec2 b) { return {wasm_i64x2_add(a.v, b.v)}; }
    friend vec2 operator-(vec2 a, vec2 b) { return {wasm_i64x2_sub(a.v, b.v)}; }
    friend vec2 operator&(vec2 a, vec2 b) { return {wasm_v128_and(a.v, b.v)}; }
    friend vec2 operator|(vec2 a, vec2 b) { return {wasm_v128_or(a.v, b.v)}; }
    friend vec2 operator^(vec2 a, vec2 b) { return {wasm_v128_xor(a.v, b.v)}; }
    friend vec2 operator~(vec2 a) { return {wasm_v128_not(a.v)}; }
    template <int S> friend vec2 shr(vec2 a) { return {wasm_u64x2_shr(a.v, S)}; }
    template <int S> friend vec2 shl(vec2 a) { return {wasm_i64x2_shl(a.v, S)}; }
    // wasm_v128_andnot(x, y) is x & ~y.
    friend vec2 andnot(vec2 a, vec2 b) { return {wasm_v128_andnot(b.v, a.v)}; }  // (~a) & b

    // acc + low/high 52 bits of (a[51:0] * b[51:0]).
    friend vec2 madd52lo(vec2 acc, vec2 a, vec2 b) {
        products const p = multiply(a, b);
        return {wasm_i64x2_add(acc.v, wasm_v128_and(p.low, wasm_i64x2_const_splat(mask52)))};
    }
    friend vec2 madd52hi(vec2 acc, vec2 a, vec2 b) {
        products const p = multiply(a, b);
        v128_t const high = wasm_i64x2_add(wasm_i64x2_add(p.p11, wasm_u64x2_shr(p.mid, 26)),
                                           wasm_u64x2_shr(p.low, 52));
        return {wasm_i64x2_add(acc.v, high)};
    }

    // Masks are plain vectors of 0 / ~0.
    friend vec2 cmpeq(vec2 a, vec2 b) { return {wasm_i64x2_eq(a.v, b.v)}; }
    // Unsigned. SIMD128 only compares 64-bit lanes as signed, so both sides
    // have their sign bit flipped first, which maps unsigned order onto signed.
    friend vec2 cmpgt(vec2 a, vec2 b) {
        v128_t const sign = wasm_i64x2_const_splat(std::int64_t(0x8000000000000000ULL));
        return {wasm_i64x2_gt(wasm_v128_xor(a.v, sign), wasm_v128_xor(b.v, sign))};
    }
    // wasm_v128_bitselect(x, y, m) is (x & m) | (y & ~m).
    friend vec2 blend(vec2 m, vec2 a, vec2 b) { return {wasm_v128_bitselect(a.v, b.v, m.v)}; }  // m ? a : b

    // base[idx] with a per-lane index. SIMD128 has no gather.
    friend vec2 gather(std::uint64_t const* base, vec2 idx) {
        return {wasm_i64x2_make(
            static_cast<std::int64_t>(base[static_cast<std::uint64_t>(wasm_i64x2_extract_lane(idx.v, 0))]),
            static_cast<std::int64_t>(base[static_cast<std::uint64_t>(wasm_i64x2_extract_lane(idx.v, 1))]))};
    }

    static constexpr std::uint64_t mask52 = 0xFFFFFFFFFFFFFULL;
    static constexpr std::uint64_t mask26 = 0x3FFFFFFULL;

private:
    // What both halves of a 52x52 product are assembled from.
    struct products {
        v128_t p11;   // a1*b1
        v128_t mid;   // a1*b0 + a0*b1
        v128_t low;   // a0*b0 + (mid mod 2^26) * 2^26
    };

    // The low 32 bits of each 64-bit lane, moved to where extmul reads them.
    static v128_t halves(v128_t x) { return wasm_i32x4_shuffle(x, x, 0, 2, 0, 2); }

    // Only the low 52 bits of each operand are read, as madd52 does.
    static products multiply(vec2 a, vec2 b) {
        v128_t const m26 = wasm_i64x2_const_splat(mask26);
        v128_t const a0 = halves(wasm_v128_and(a.v, m26));
        v128_t const a1 = halves(wasm_v128_and(wasm_u64x2_shr(a.v, 26), m26));
        v128_t const b0 = halves(wasm_v128_and(b.v, m26));
        v128_t const b1 = halves(wasm_v128_and(wasm_u64x2_shr(b.v, 26), m26));

        v128_t const p00 = wasm_u64x2_extmul_low_u32x4(a0, b0);
        v128_t const mid = wasm_i64x2_add(wasm_u64x2_extmul_low_u32x4(a1, b0),
                                          wasm_u64x2_extmul_low_u32x4(a0, b1));
        v128_t const low = wasm_i64x2_add(p00, wasm_i64x2_shl(wasm_v128_and(mid, m26), 26));
        return {wasm_u64x2_extmul_low_u32x4(a1, b1), mid, low};
    }
};

} // namespace secp256k1_turbo

#endif
