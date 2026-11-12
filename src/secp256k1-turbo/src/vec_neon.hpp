// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Two-lane backend for ARM NEON (AArch64).
//
// NEON has no 64x64 lane multiply and nothing like AVX-512 IFMA's madd52: the
// widest lane product is 32x32 -> 64 (umull). The kernel is written against
// madd52lo / madd52hi, so both are built here from four 26x26 partial
// products. With a = a1*2^26 + a0 and b = b1*2^26 + b0, all four halves below
// 2^26:
//
//   a*b = a1*b1 * 2^52  +  (a1*b0 + a0*b1) * 2^26  +  a0*b0
//
// The low 52 bits come from a0*b0 and the low half of the middle term; the
// high 52 bits are a1*b1, the high half of the middle term, and the carry out
// of the low sum. Every intermediate fits in 64 bits: the partial products are
// below 2^52, the middle term below 2^53, the low sum below 2^53.
//
// That is the same algorithm as the other backends, bit for bit, at four
// multiplies per product instead of one. It exists so the lane kernel runs,
// and can be measured, where there is no wider multiply; whether it is worth
// selecting on a given core is a measurement, and best_backend() does not
// choose it.

#ifndef KTH_SECP256K1_TURBO_VEC_NEON_HPP
#define KTH_SECP256K1_TURBO_VEC_NEON_HPP

#include <cstddef>
#include <cstdint>

#include <arm_neon.h>

namespace secp256k1_turbo {

struct vec2 {
    static constexpr std::size_t lanes = 2;
    uint64x2_t v;

    static vec2 zero() { return {vdupq_n_u64(0)}; }
    static vec2 set1(std::uint64_t x) { return {vdupq_n_u64(x)}; }
    static vec2 load(std::uint64_t const* p) { return {vld1q_u64(p)}; }
    void store(std::uint64_t* p) const { vst1q_u64(p, v); }
    std::uint64_t lane(std::size_t i) const {
        std::uint64_t t[2];
        vst1q_u64(t, v);
        return t[i];
    }

    friend vec2 operator+(vec2 a, vec2 b) { return {vaddq_u64(a.v, b.v)}; }
    friend vec2 operator-(vec2 a, vec2 b) { return {vsubq_u64(a.v, b.v)}; }
    friend vec2 operator&(vec2 a, vec2 b) { return {vandq_u64(a.v, b.v)}; }
    friend vec2 operator|(vec2 a, vec2 b) { return {vorrq_u64(a.v, b.v)}; }
    friend vec2 operator^(vec2 a, vec2 b) { return {veorq_u64(a.v, b.v)}; }
    friend vec2 operator~(vec2 a) { return {veorq_u64(a.v, vdupq_n_u64(~std::uint64_t(0)))}; }
    // By a run-time count rather than vshrq_n / vshlq_n: those take the count
    // as an immediate with a restricted range (a right shift of 0 is not
    // encodable), and the kernel's shift amounts are template arguments.
    template <int S> friend vec2 shr(vec2 a) { return {vshlq_u64(a.v, vdupq_n_s64(-S))}; }
    template <int S> friend vec2 shl(vec2 a) { return {vshlq_u64(a.v, vdupq_n_s64(S))}; }
    friend vec2 andnot(vec2 a, vec2 b) { return {vbicq_u64(b.v, a.v)}; }  // (~a) & b

    // acc + low/high 52 bits of (a[51:0] * b[51:0]).
    friend vec2 madd52lo(vec2 acc, vec2 a, vec2 b) {
        products const p = multiply(a, b);
        return {vaddq_u64(acc.v, vandq_u64(p.low, vdupq_n_u64(mask52)))};
    }
    friend vec2 madd52hi(vec2 acc, vec2 a, vec2 b) {
        products const p = multiply(a, b);
        uint64x2_t const high = vaddq_u64(vaddq_u64(p.p11, vshrq_n_u64(p.mid, 26)), vshrq_n_u64(p.low, 52));
        return {vaddq_u64(acc.v, high)};
    }

    // Masks are plain vectors of 0 / ~0.
    friend vec2 cmpeq(vec2 a, vec2 b) { return {vceqq_u64(a.v, b.v)}; }
    friend vec2 cmpgt(vec2 a, vec2 b) { return {vcgtq_u64(a.v, b.v)}; }  // unsigned
    friend vec2 blend(vec2 m, vec2 a, vec2 b) { return {vbslq_u64(m.v, a.v, b.v)}; }  // m ? a : b

    // base[idx] with a per-lane index. NEON has no gather.
    friend vec2 gather(std::uint64_t const* base, vec2 idx) {
        return {vcombine_u64(vcreate_u64(base[vgetq_lane_u64(idx.v, 0)]),
                             vcreate_u64(base[vgetq_lane_u64(idx.v, 1)]))};
    }

    static constexpr std::uint64_t mask52 = 0xFFFFFFFFFFFFFULL;
    static constexpr std::uint64_t mask26 = 0x3FFFFFFULL;

private:
    // What both halves of a 52x52 product are assembled from.
    struct products {
        uint64x2_t p11;   // a1*b1
        uint64x2_t mid;   // a1*b0 + a0*b1
        uint64x2_t low;   // a0*b0 + (mid mod 2^26) * 2^26
    };

    // Only the low 52 bits of each operand are read, as madd52 does.
    static products multiply(vec2 a, vec2 b) {
        uint64x2_t const m26 = vdupq_n_u64(mask26);
        uint32x2_t const a0 = vmovn_u64(vandq_u64(a.v, m26));
        uint32x2_t const a1 = vmovn_u64(vandq_u64(vshrq_n_u64(a.v, 26), m26));
        uint32x2_t const b0 = vmovn_u64(vandq_u64(b.v, m26));
        uint32x2_t const b1 = vmovn_u64(vandq_u64(vshrq_n_u64(b.v, 26), m26));

        uint64x2_t const p00 = vmull_u32(a0, b0);
        uint64x2_t const mid = vaddq_u64(vmull_u32(a1, b0), vmull_u32(a0, b1));
        uint64x2_t const low = vaddq_u64(p00, vshlq_n_u64(vandq_u64(mid, m26), 26));
        return {vmull_u32(a1, b1), mid, low};
    }
};

} // namespace secp256k1_turbo

#endif
