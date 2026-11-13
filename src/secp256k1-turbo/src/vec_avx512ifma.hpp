// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Eight-lane AVX-512 IFMA backend. Only included from the translation unit
// that is compiled with the AVX-512 flags; the dispatcher never calls into it
// unless the running CPU reports AVX512F/DQ/BW/VL/IFMA.

#ifndef KTH_SECP256K1_TURBO_VEC_AVX512IFMA_HPP
#define KTH_SECP256K1_TURBO_VEC_AVX512IFMA_HPP

#include <cstddef>
#include <cstdint>
#include <immintrin.h>

namespace secp256k1_turbo {

struct vec8 {
    static constexpr std::size_t lanes = 8;
    __m512i v;

    static vec8 zero() { return {_mm512_setzero_si512()}; }
    static vec8 set1(std::uint64_t x) { return {_mm512_set1_epi64(static_cast<long long>(x))}; }
    static vec8 load(std::uint64_t const* p) { return {_mm512_loadu_si512(p)}; }
    void store(std::uint64_t* p) const { _mm512_storeu_si512(p, v); }
    std::uint64_t lane(std::size_t i) const {
        alignas(64) std::uint64_t t[8];
        _mm512_store_si512(t, v);
        return t[i];
    }

    friend vec8 operator+(vec8 a, vec8 b) { return {_mm512_add_epi64(a.v, b.v)}; }
    friend vec8 operator-(vec8 a, vec8 b) { return {_mm512_sub_epi64(a.v, b.v)}; }
    friend vec8 operator&(vec8 a, vec8 b) { return {_mm512_and_si512(a.v, b.v)}; }
    friend vec8 operator|(vec8 a, vec8 b) { return {_mm512_or_si512(a.v, b.v)}; }
    friend vec8 operator^(vec8 a, vec8 b) { return {_mm512_xor_si512(a.v, b.v)}; }
    friend vec8 operator~(vec8 a) { return {_mm512_xor_si512(a.v, _mm512_set1_epi64(-1))}; }
    template <int S> friend vec8 shr(vec8 a) { return {_mm512_srli_epi64(a.v, S)}; }
    template <int S> friend vec8 shl(vec8 a) { return {_mm512_slli_epi64(a.v, S)}; }
    friend vec8 andnot(vec8 a, vec8 b) { return {_mm512_andnot_si512(a.v, b.v)}; }

    friend vec8 madd52lo(vec8 acc, vec8 a, vec8 b) { return {_mm512_madd52lo_epu64(acc.v, a.v, b.v)}; }
    friend vec8 madd52hi(vec8 acc, vec8 a, vec8 b) { return {_mm512_madd52hi_epu64(acc.v, a.v, b.v)}; }

    friend vec8 cmpeq(vec8 a, vec8 b) { return {_mm512_movm_epi64(_mm512_cmpeq_epu64_mask(a.v, b.v))}; }
    friend vec8 cmpgt(vec8 a, vec8 b) { return {_mm512_movm_epi64(_mm512_cmpgt_epu64_mask(a.v, b.v))}; }
    friend vec8 blend(vec8 m, vec8 a, vec8 b) {
        // (a & m) | (b & ~m) in a single vpternlogq.
        return {_mm512_ternarylogic_epi64(m.v, a.v, b.v, 0xCA)};
    }

    friend vec8 gather(std::uint64_t const* base, vec8 idx) {
        return {_mm512_i64gather_epi64(idx.v, base, 8)};
    }

    static constexpr std::uint64_t mask52 = 0xFFFFFFFFFFFFFULL;
};

} // namespace secp256k1_turbo

#endif
