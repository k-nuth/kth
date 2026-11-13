// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// GPU verification kernel, one thread per signature. This file is the single
// source of the device code: it is compiled as CUDA device code, as Metal
// Shading Language (embedded as a string and compiled at run time) and as
// plain C++ for the host reference used by the tests. It therefore sticks to
// the common subset of the three: no standard headers, no templates, no
// recursion, explicit address-space macros.
//
// Field arithmetic is radix 2^32 with 8 limbs and 64-bit accumulators,
// because GPUs have 32x32->64 multipliers and no 64x64 ones. Values are kept
// below 2^256 but not necessarily below p (lazy reduction: 2^256 - p is tiny,
// so the only non-canonical representative of a value v is v + p);
// comparisons account for that and only the parity test fully normalizes.
// (A radix 2^26 variant with carry-free column sums was measured as well: no
// faster on NVIDIA, 2.5x slower on Apple GPUs, where 64-bit integer
// arithmetic is emulated.) The group law, the GLV decomposition, the comb
// table for G and the final checks are the same algorithm as the
// lane-parallel CPU kernel (src/kernel.hpp), so the two are interchangeable
// per signature.

#ifndef KTH_SECP256K1_TURBO_GPU_KERNEL_CORE_H
#define KTH_SECP256K1_TURBO_GPU_KERNEL_CORE_H

#if defined(__METAL_VERSION__)
    typedef uint u32;
    typedef ulong u64;
    typedef int i32;
    typedef long i64;
    #define GPU_FN inline __attribute__((always_inline))
    #define GPU_THREAD thread
    #define GPU_GLOBAL device
    #define GPU_GTAB constant u32*
#elif defined(__CUDACC__)
    typedef unsigned int u32;
    typedef unsigned long long u64;
    typedef int i32;
    typedef long long i64;
    #define GPU_FN __device__ __forceinline__
    #define GPU_THREAD
    #define GPU_GLOBAL
    #define GPU_GTAB u32 const*
#else
    #include <cstdint>
    typedef std::uint32_t u32;
    typedef std::uint64_t u64;
    typedef std::int32_t i32;
    typedef std::int64_t i64;
    #define GPU_FN static inline
    #define GPU_THREAD
    #define GPU_GLOBAL
    #define GPU_GTAB u32 const*
#endif

// Per-signature input, filled on the host (little-endian 32-bit limbs).
#define GPU_FLAG_OK         1u   // structurally valid; otherwise the verdict is 0
#define GPU_FLAG_DECOMPRESS 2u   // derive y from x
#define GPU_FLAG_PARITY_ODD 4u   // parity of y: declared (compressed) or of the given y
#define GPU_FLAG_USE_RN     8u   // ECDSA: also accept X == (r + n) Z^2

// One signature as the node hands it over: public key words (x, and y
// unless the key is compressed), the signature scalars and the message hash,
// all as 8 little-endian 32-bit words straight from the big-endian bytes.
// The device does every check and derivation (the Schnorr challenge hash,
// ranges, s^-1, u1, u2, the GLV split, r + n).
struct gpu_raw_input {
    u32 qx[8];
    u32 qy[8];
    u32 r[8];
    u32 s[8];     // ECDSA and Schnorr s
    u32 z[8];     // message hash
    u32 flags;
};

// G comb table layout in device memory: limb-major, g[limb * 256 + entry],
// limbs 0..7 = x, 8..15 = y of entry d = c + 16 c' (c G + c' 2^128 G).
// Entry 0 is unused.
#define GPU_G_ENTRIES 256
#define GPU_G_LIMBS 16
#define GPU_G_WORDS (GPU_G_LIMBS * GPU_G_ENTRIES)

struct fe8 { u32 n[8]; };
struct ge8 { fe8 x, y; };
struct gej8 { fe8 x, y, z; u32 inf; };

#define FE_P0 0xFFFFFC2Fu
#define FE_P1 0xFFFFFFFEu
#define FE_PH 0xFFFFFFFFu
#define FE_C  977u   // 2^256 = 2^32 + 977 (mod p)

GPU_FN u32 fe_p_limb(int i) { return i == 0 ? FE_P0 : (i == 1 ? FE_P1 : FE_PH); }
GPU_FN u32 gpu_mask(u32 cond) { return cond ? 0xFFFFFFFFu : 0u; }

GPU_FN fe8 fe_zero() { fe8 r; for (int i = 0; i < 8; ++i) r.n[i] = 0; return r; }
GPU_FN fe8 fe_one() { fe8 r = fe_zero(); r.n[0] = 1; return r; }
GPU_FN fe8 fe_small(u32 v) { fe8 r = fe_zero(); r.n[0] = v; return r; }
// 8 little-endian 32-bit words (the limbs themselves).
GPU_FN fe8 fe_from_words(const GPU_GLOBAL u32* w) { fe8 r; for (int i = 0; i < 8; ++i) r.n[i] = w[i]; return r; }

// m ? a : b with m a full mask (0 or ~0).
GPU_FN fe8 fe_select(u32 m, fe8 a, fe8 b) {
    fe8 r;
    for (int i = 0; i < 8; ++i) r.n[i] = (a.n[i] & m) | (b.n[i] & ~m);
    return r;
}

#if defined(__CUDACC__)
// Folds r + h * 2^256 (h < 2^34, r < 2^256) below 2^256 through
// 2^256 == 2^32 + 977 (mod p): three carry chains add h * 977, its high
// words and h itself one word up; a second one-word pass absorbs the carry
// out (0 or 1), which cannot carry out again.
GPU_FN fe8 fe_fold(GPU_THREAD u32* r, u64 h) {
    u32 const hlo = (u32)h, hhi = (u32)(h >> 32);
    u32 k;
    fe8 o;
    asm(
        "mad.lo.cc.u32 %0, %9, 977, %11;\n\t"
        "madc.lo.cc.u32 %1, %10, 977, %12;\n\t"
        "addc.cc.u32 %2, %13, 0;\n\t"
        "addc.cc.u32 %3, %14, 0;\n\t"
        "addc.cc.u32 %4, %15, 0;\n\t"
        "addc.cc.u32 %5, %16, 0;\n\t"
        "addc.cc.u32 %6, %17, 0;\n\t"
        "addc.cc.u32 %7, %18, 0;\n\t"
        "addc.u32 %8, 0, 0;\n\t"
        "mad.hi.cc.u32 %1, %9, 977, %1;\n\t"
        "madc.hi.cc.u32 %2, %10, 977, %2;\n\t"
        "addc.cc.u32 %3, %3, 0;\n\t"
        "addc.cc.u32 %4, %4, 0;\n\t"
        "addc.cc.u32 %5, %5, 0;\n\t"
        "addc.cc.u32 %6, %6, 0;\n\t"
        "addc.cc.u32 %7, %7, 0;\n\t"
        "addc.u32 %8, %8, 0;\n\t"
        "add.cc.u32 %1, %1, %9;\n\t"
        "addc.cc.u32 %2, %2, %10;\n\t"
        "addc.cc.u32 %3, %3, 0;\n\t"
        "addc.cc.u32 %4, %4, 0;\n\t"
        "addc.cc.u32 %5, %5, 0;\n\t"
        "addc.cc.u32 %6, %6, 0;\n\t"
        "addc.cc.u32 %7, %7, 0;\n\t"
        "addc.u32 %8, %8, 0;\n\t"
        "mad.lo.cc.u32 %0, %8, 977, %0;\n\t"
        "addc.cc.u32 %1, %1, %8;\n\t"
        "addc.cc.u32 %2, %2, 0;\n\t"
        "addc.cc.u32 %3, %3, 0;\n\t"
        "addc.cc.u32 %4, %4, 0;\n\t"
        "addc.cc.u32 %5, %5, 0;\n\t"
        "addc.cc.u32 %6, %6, 0;\n\t"
        "addc.u32 %7, %7, 0;"
        : "=&r"(o.n[0]), "=&r"(o.n[1]), "=&r"(o.n[2]), "=&r"(o.n[3]), "=&r"(o.n[4]), "=&r"(o.n[5]), "=&r"(o.n[6]), "=&r"(o.n[7]), "=&r"(k)
        : "r"(hlo), "r"(hhi), "r"(r[0]), "r"(r[1]), "r"(r[2]), "r"(r[3]), "r"(r[4]), "r"(r[5]), "r"(r[6]), "r"(r[7]));
    return o;
}
// Same for a one-word h (h < 2^31): one chain plus the carry pass.
GPU_FN fe8 fe_fold1(GPU_THREAD u32* r, u32 h) {
    u32 k;
    fe8 o;
    asm(
        "mad.lo.cc.u32 %0, %9, 977, %10;\n\t"
        "madc.hi.cc.u32 %1, %9, 977, %11;\n\t"
        "addc.cc.u32 %2, %12, 0;\n\t"
        "addc.cc.u32 %3, %13, 0;\n\t"
        "addc.cc.u32 %4, %14, 0;\n\t"
        "addc.cc.u32 %5, %15, 0;\n\t"
        "addc.cc.u32 %6, %16, 0;\n\t"
        "addc.cc.u32 %7, %17, 0;\n\t"
        "addc.u32 %8, 0, 0;\n\t"
        "add.cc.u32 %1, %1, %9;\n\t"
        "addc.cc.u32 %2, %2, 0;\n\t"
        "addc.cc.u32 %3, %3, 0;\n\t"
        "addc.cc.u32 %4, %4, 0;\n\t"
        "addc.cc.u32 %5, %5, 0;\n\t"
        "addc.cc.u32 %6, %6, 0;\n\t"
        "addc.cc.u32 %7, %7, 0;\n\t"
        "addc.u32 %8, %8, 0;\n\t"
        "mad.lo.cc.u32 %0, %8, 977, %0;\n\t"
        "addc.cc.u32 %1, %1, %8;\n\t"
        "addc.cc.u32 %2, %2, 0;\n\t"
        "addc.cc.u32 %3, %3, 0;\n\t"
        "addc.cc.u32 %4, %4, 0;\n\t"
        "addc.cc.u32 %5, %5, 0;\n\t"
        "addc.cc.u32 %6, %6, 0;\n\t"
        "addc.u32 %7, %7, 0;"
        : "=&r"(o.n[0]), "=&r"(o.n[1]), "=&r"(o.n[2]), "=&r"(o.n[3]), "=&r"(o.n[4]), "=&r"(o.n[5]), "=&r"(o.n[6]), "=&r"(o.n[7]), "=&r"(k)
        : "r"(h), "r"(r[0]), "r"(r[1]), "r"(r[2]), "r"(r[3]), "r"(r[4]), "r"(r[5]), "r"(r[6]), "r"(r[7]));
    return o;
}
#else
// Folds r + h * 2^256 (h < 2^34, r < 2^256) below 2^256 through
// 2^256 == 2^32 + 977 (mod p). The result is not necessarily below p.
GPU_FN fe8 fe_fold(GPU_THREAD u32* r, u64 h) {
    u64 x = (u64)r[0] + h * FE_C;              r[0] = (u32)x; u64 c = x >> 32;
    x = (u64)r[1] + (u32)h + c;                r[1] = (u32)x; c = x >> 32;
    x = (u64)r[2] + (u32)(h >> 32) + c;        r[2] = (u32)x; c = x >> 32;
    for (int i = 3; i < 8; ++i) { x = (u64)r[i] + c; r[i] = (u32)x; c = x >> 32; }
    // c is 0 or 1 and the value is then below 2^67: a second fold cannot carry out.
    u32 const k = (u32)c;
    fe8 out;
    x = (u64)r[0] + (u64)k * FE_C;             out.n[0] = (u32)x; c = x >> 32;
    x = (u64)r[1] + k + c;                     out.n[1] = (u32)x; c = x >> 32;
    for (int i = 2; i < 8; ++i) { x = (u64)r[i] + c; out.n[i] = (u32)x; c = x >> 32; }
    return out;
}

#endif

// Canonical representative below p (one conditional subtraction).
GPU_FN fe8 fe_normalize(fe8 a) {
    fe8 s;
    u64 b = 0;
    for (int i = 0; i < 8; ++i) {
        u64 d = (u64)a.n[i] - fe_p_limb(i) - b;
        s.n[i] = (u32)d;
        b = (d >> 32) & 1u;
    }
    return fe_select(gpu_mask(b == 0), s, a);
}
GPU_FN void fe_to_words(fe8 a, GPU_GLOBAL u32* w) {
    a = fe_normalize(a);
    for (int i = 0; i < 8; ++i) w[i] = a.n[i];
}

// Mask: a == 0 mod p (a is 0 or p).
GPU_FN u32 fe_is_zero(fe8 a) {
    u32 o = 0, d = 0;
    for (int i = 0; i < 8; ++i) { o |= a.n[i]; d |= a.n[i] ^ fe_p_limb(i); }
    return gpu_mask(o == 0 || d == 0);
}
// a < p for the plain 8-word value (the canonical test for parsed inputs).
GPU_FN u32 fe_lt_p(fe8 a) {
    for (int i = 7; i >= 0; --i) {
        u32 const pl = fe_p_limb(i);
        if (a.n[i] != pl) return a.n[i] < pl;
    }
    return 0;
}
GPU_FN u32 fe_is_odd(fe8 a) { return 0u - (fe_normalize(a).n[0] & 1u); }

#if defined(__CUDACC__)
GPU_FN fe8 fe_add(fe8 a, fe8 b) {
    u32 r[8], c;
    asm(
        "add.cc.u32 %0, %9, %17;\n\t"
        "addc.cc.u32 %1, %10, %18;\n\t"
        "addc.cc.u32 %2, %11, %19;\n\t"
        "addc.cc.u32 %3, %12, %20;\n\t"
        "addc.cc.u32 %4, %13, %21;\n\t"
        "addc.cc.u32 %5, %14, %22;\n\t"
        "addc.cc.u32 %6, %15, %23;\n\t"
        "addc.cc.u32 %7, %16, %24;\n\t"
        "addc.u32 %8, 0, 0;"
        : "=&r"(r[0]), "=&r"(r[1]), "=&r"(r[2]), "=&r"(r[3]), "=&r"(r[4]), "=&r"(r[5]), "=&r"(r[6]), "=&r"(r[7]), "=&r"(c)
        : "r"(a.n[0]), "r"(a.n[1]), "r"(a.n[2]), "r"(a.n[3]), "r"(a.n[4]), "r"(a.n[5]), "r"(a.n[6]), "r"(a.n[7]),
          "r"(b.n[0]), "r"(b.n[1]), "r"(b.n[2]), "r"(b.n[3]), "r"(b.n[4]), "r"(b.n[5]), "r"(b.n[6]), "r"(b.n[7]));
    return fe_fold1(r, c);
}
// a - b as a + 2p - b: 2p = 2^257 - 2^33 - 1954 exceeds any b below 2^256.
GPU_FN fe8 fe_sub(fe8 a, fe8 b) {
    u32 r[8], c;
    asm(
        "add.cc.u32 %0, %9, 0xFFFFF85E;\n\t"
        "addc.cc.u32 %1, %10, 0xFFFFFFFD;\n\t"
        "addc.cc.u32 %2, %11, 0xFFFFFFFF;\n\t"
        "addc.cc.u32 %3, %12, 0xFFFFFFFF;\n\t"
        "addc.cc.u32 %4, %13, 0xFFFFFFFF;\n\t"
        "addc.cc.u32 %5, %14, 0xFFFFFFFF;\n\t"
        "addc.cc.u32 %6, %15, 0xFFFFFFFF;\n\t"
        "addc.cc.u32 %7, %16, 0xFFFFFFFF;\n\t"
        "addc.u32 %8, 1, 0;\n\t"
        "sub.cc.u32 %0, %0, %17;\n\t"
        "subc.cc.u32 %1, %1, %18;\n\t"
        "subc.cc.u32 %2, %2, %19;\n\t"
        "subc.cc.u32 %3, %3, %20;\n\t"
        "subc.cc.u32 %4, %4, %21;\n\t"
        "subc.cc.u32 %5, %5, %22;\n\t"
        "subc.cc.u32 %6, %6, %23;\n\t"
        "subc.cc.u32 %7, %7, %24;\n\t"
        "subc.u32 %8, %8, 0;"
        : "=&r"(r[0]), "=&r"(r[1]), "=&r"(r[2]), "=&r"(r[3]), "=&r"(r[4]), "=&r"(r[5]), "=&r"(r[6]), "=&r"(r[7]), "=&r"(c)
        : "r"(a.n[0]), "r"(a.n[1]), "r"(a.n[2]), "r"(a.n[3]), "r"(a.n[4]), "r"(a.n[5]), "r"(a.n[6]), "r"(a.n[7]),
          "r"(b.n[0]), "r"(b.n[1]), "r"(b.n[2]), "r"(b.n[3]), "r"(b.n[4]), "r"(b.n[5]), "r"(b.n[6]), "r"(b.n[7]));
    return fe_fold1(r, c);
}
#else
GPU_FN fe8 fe_add(fe8 a, fe8 b) {
    u32 r[8];
    u64 c = 0;
    for (int i = 0; i < 8; ++i) { u64 x = (u64)a.n[i] + b.n[i] + c; r[i] = (u32)x; c = x >> 32; }
    return fe_fold(r, c);
}
// a - b as a + 2p - b: 2p exceeds any b below 2^256, so no borrow.
GPU_FN fe8 fe_sub(fe8 a, fe8 b) {
    u32 r[8];
    u64 c = 0;
    for (int i = 0; i < 8; ++i) { u64 x = (u64)a.n[i] + 2 * (u64)fe_p_limb(i) + c; r[i] = (u32)x; c = x >> 32; }
    u64 bw = 0;
    for (int i = 0; i < 8; ++i) { u64 d = (u64)r[i] - b.n[i] - bw; r[i] = (u32)d; bw = (d >> 32) & 1u; }
    return fe_fold(r, c - bw);
}
#endif
GPU_FN fe8 fe_neg(fe8 a) { return fe_sub(fe_zero(), a); }
GPU_FN fe8 fe_cneg(u32 m, fe8 a) { return fe_select(m, fe_neg(a), a); }

#if defined(__CUDACC__)
// a * k for a small k (k <= 8): low halves, then the high halves one word up.
GPU_FN fe8 fe_mul_small(fe8 a, u32 k) {
    u32 r[8], c;
    asm(
        "mul.lo.u32 %0, %9, %17;\n\t"
        "mul.lo.u32 %1, %10, %17;\n\t"
        "mul.lo.u32 %2, %11, %17;\n\t"
        "mul.lo.u32 %3, %12, %17;\n\t"
        "mul.lo.u32 %4, %13, %17;\n\t"
        "mul.lo.u32 %5, %14, %17;\n\t"
        "mul.lo.u32 %6, %15, %17;\n\t"
        "mul.lo.u32 %7, %16, %17;\n\t"
        "mad.hi.cc.u32 %1, %9, %17, %1;\n\t"
        "madc.hi.cc.u32 %2, %10, %17, %2;\n\t"
        "madc.hi.cc.u32 %3, %11, %17, %3;\n\t"
        "madc.hi.cc.u32 %4, %12, %17, %4;\n\t"
        "madc.hi.cc.u32 %5, %13, %17, %5;\n\t"
        "madc.hi.cc.u32 %6, %14, %17, %6;\n\t"
        "madc.hi.cc.u32 %7, %15, %17, %7;\n\t"
        "madc.hi.u32 %8, %16, %17, 0;"
        : "=&r"(r[0]), "=&r"(r[1]), "=&r"(r[2]), "=&r"(r[3]), "=&r"(r[4]), "=&r"(r[5]), "=&r"(r[6]), "=&r"(r[7]), "=&r"(c)
        : "r"(a.n[0]), "r"(a.n[1]), "r"(a.n[2]), "r"(a.n[3]), "r"(a.n[4]), "r"(a.n[5]), "r"(a.n[6]), "r"(a.n[7]), "r"(k));
    return fe_fold1(r, c);
}
#else
// a * k for a small k (k <= 8).
GPU_FN fe8 fe_mul_small(fe8 a, u32 k) {
    u32 r[8];
    u64 c = 0;
    for (int i = 0; i < 8; ++i) { u64 x = (u64)a.n[i] * k + c; r[i] = (u32)x; c = x >> 32; }
    return fe_fold(r, c);
}

#endif

// a / 2 mod p: (a + p) / 2 when the representative is odd. a + p < 2^257
// fits 9 limbs and the halved value is below 2^256.
GPU_FN fe8 fe_half(fe8 a) {
    u32 const odd = 0u - (a.n[0] & 1u);
    u32 t[9];
    u64 c = 0;
    for (int i = 0; i < 8; ++i) { u64 x = (u64)a.n[i] + (fe_p_limb(i) & odd) + c; t[i] = (u32)x; c = x >> 32; }
    t[8] = (u32)c;
    fe8 r;
    for (int i = 0; i < 8; ++i) r.n[i] = (t[i] >> 1) | (t[i + 1] << 31);
    return r;
}

#if defined(__CUDACC__)
// CUDA: the 512-bit product is accumulated column by column (Comba) in a
// three-word accumulator with the carry-chain instructions mad.lo.cc /
// madc.hi.cc / addc, and reduced with three short chains. Written row by
// row in C++, the compiler serialises the 64 partial products behind one
// carry; this form is 1.6x faster per multiplication and 1.4x per squaring.
#define FE_MACC(x, y) asm("mad.lo.cc.u32 %0, %3, %4, %0;\n\tmadc.hi.cc.u32 %1, %3, %4, %1;\n\taddc.u32 %2, %2, 0;" : "+r"(c0), "+r"(c1), "+r"(c2) : "r"(x), "r"(y))

// r = L + H * (2^32 + 977) for the 16-word product t, then the one- or
// two-word overflow is folded by fe_fold.
GPU_FN fe8 fe_reduce512(const GPU_THREAD u32* t) {
    u32 r[8];
    u32 c, chi;
    asm(
        "mad.lo.cc.u32 %0, %10, 977, %18;\n\t"
        "madc.lo.cc.u32 %1, %11, 977, %19;\n\t"
        "madc.lo.cc.u32 %2, %12, 977, %20;\n\t"
        "madc.lo.cc.u32 %3, %13, 977, %21;\n\t"
        "madc.lo.cc.u32 %4, %14, 977, %22;\n\t"
        "madc.lo.cc.u32 %5, %15, 977, %23;\n\t"
        "madc.lo.cc.u32 %6, %16, 977, %24;\n\t"
        "madc.lo.cc.u32 %7, %17, 977, %25;\n\t"
        "addc.u32 %8, 0, 0;\n\t"
        "mad.hi.cc.u32 %1, %10, 977, %1;\n\t"
        "madc.hi.cc.u32 %2, %11, 977, %2;\n\t"
        "madc.hi.cc.u32 %3, %12, 977, %3;\n\t"
        "madc.hi.cc.u32 %4, %13, 977, %4;\n\t"
        "madc.hi.cc.u32 %5, %14, 977, %5;\n\t"
        "madc.hi.cc.u32 %6, %15, 977, %6;\n\t"
        "madc.hi.cc.u32 %7, %16, 977, %7;\n\t"
        "madc.hi.u32 %8, %17, 977, %8;\n\t"
        "add.cc.u32 %1, %1, %10;\n\t"
        "addc.cc.u32 %2, %2, %11;\n\t"
        "addc.cc.u32 %3, %3, %12;\n\t"
        "addc.cc.u32 %4, %4, %13;\n\t"
        "addc.cc.u32 %5, %5, %14;\n\t"
        "addc.cc.u32 %6, %6, %15;\n\t"
        "addc.cc.u32 %7, %7, %16;\n\t"
        "addc.cc.u32 %8, %8, %17;\n\t"
        "addc.u32 %9, 0, 0;"
        : "=&r"(r[0]), "=&r"(r[1]), "=&r"(r[2]), "=&r"(r[3]), "=&r"(r[4]), "=&r"(r[5]), "=&r"(r[6]), "=&r"(r[7]), "=&r"(c), "=&r"(chi)
        : "r"(t[8]), "r"(t[9]), "r"(t[10]), "r"(t[11]), "r"(t[12]), "r"(t[13]), "r"(t[14]), "r"(t[15]),
          "r"(t[0]), "r"(t[1]), "r"(t[2]), "r"(t[3]), "r"(t[4]), "r"(t[5]), "r"(t[6]), "r"(t[7]));
    return fe_fold(r, ((u64)chi << 32) | c);
}

GPU_FN fe8 fe_mul(fe8 a, fe8 b) {
    u32 t[16];
    u32 c0 = 0, c1 = 0, c2 = 0;
    for (int k = 0; k < 15; ++k) {
        int const lo_i = k < 8 ? 0 : k - 7, hi_i = k < 8 ? k : 7;
        for (int i = lo_i; i <= hi_i; ++i) FE_MACC(a.n[i], b.n[k - i]);
        t[k] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    t[15] = c0;
    return fe_reduce512(t);
}

// Each off-diagonal product once, the column doubled, then the square on the
// diagonal; the carry words of the previous column join after the doubling.
GPU_FN fe8 fe_sqr(fe8 a) {
    u32 t[16];
    u32 c0 = 0, c1 = 0, c2 = 0;
    for (int k = 0; k < 15; ++k) {
        u32 const in0 = c0, in1 = c1;
        c0 = 0; c1 = 0; c2 = 0;
        int const lo_i = k < 8 ? 0 : k - 7;
        for (int i = lo_i; 2 * i < k; ++i) FE_MACC(a.n[i], a.n[k - i]);
        asm("add.cc.u32 %0, %0, %0;\n\taddc.cc.u32 %1, %1, %1;\n\taddc.u32 %2, %2, %2;" : "+r"(c0), "+r"(c1), "+r"(c2));
        if ((k & 1) == 0) FE_MACC(a.n[k / 2], a.n[k / 2]);
        asm("add.cc.u32 %0, %0, %3;\n\taddc.cc.u32 %1, %1, %4;\n\taddc.u32 %2, %2, 0;" : "+r"(c0), "+r"(c1), "+r"(c2) : "r"(in0), "r"(in1));
        t[k] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    t[15] = c0;
    return fe_reduce512(t);
}
#undef FE_MACC
#else
// 512-bit product t (16 limbs) reduced below 2^256.
GPU_FN fe8 fe_reduce512(const GPU_THREAD u32* t) {
    // r = L + H * (2^32 + 977)
    u32 r[8];
    u64 c = 0;
    for (int i = 0; i < 8; ++i) {
        u64 x = (u64)t[i] + (u64)t[i + 8] * FE_C + c;
        if (i > 0) x += t[i + 7];
        r[i] = (u32)x; c = x >> 32;
    }
    return fe_fold(r, c + t[15]);
}

GPU_FN fe8 fe_mul(fe8 a, fe8 b) {
    u32 t[16];
    for (int i = 0; i < 16; ++i) t[i] = 0;
    for (int i = 0; i < 8; ++i) {
        u64 c = 0;
        for (int j = 0; j < 8; ++j) {
            u64 x = (u64)a.n[i] * b.n[j] + t[i + j] + c;
            t[i + j] = (u32)x; c = x >> 32;
        }
        t[i + 8] = (u32)c;
    }
    return fe_reduce512(t);
}

GPU_FN fe8 fe_sqr(fe8 a) {
    u32 t[16];
    for (int i = 0; i < 16; ++i) t[i] = 0;
    // off-diagonal products
    for (int i = 0; i < 7; ++i) {
        u64 c = 0;
        for (int j = i + 1; j < 8; ++j) {
            u64 x = (u64)a.n[i] * a.n[j] + t[i + j] + c;
            t[i + j] = (u32)x; c = x >> 32;
        }
        t[i + 8] = (u32)c;
    }
    // doubled
    u32 carry = 0;
    for (int k = 0; k < 16; ++k) { u32 nv = (t[k] << 1) | carry; carry = t[k] >> 31; t[k] = nv; }
    // plus the squares
    u64 c = 0;
    for (int i = 0; i < 8; ++i) {
        u64 x = (u64)a.n[i] * a.n[i] + t[2 * i] + c;
        t[2 * i] = (u32)x; c = x >> 32;
        x = (u64)t[2 * i + 1] + c;
        t[2 * i + 1] = (u32)x; c = x >> 32;
    }
    return fe_reduce512(t);
}

#endif

GPU_FN u32 fe_eq(fe8 a, fe8 b) { return fe_is_zero(fe_sub(a, b)); }

// Loops over whole group or field operations stay rolled: unrolled, the
// ladder alone is about 100k instructions and the instruction cache
// thrashes, doubling the latency of every operation in it.
#if defined(__CUDACC__)
    #define GPU_NO_UNROLL _Pragma("unroll 1")
#elif defined(__METAL_VERSION__)
    #define GPU_NO_UNROLL _Pragma("clang loop unroll(disable)")
#else
    #define GPU_NO_UNROLL
#endif

// Repeated squaring.
GPU_FN fe8 fe_sqr_n(fe8 a, int n) {
    GPU_NO_UNROLL
    for (int i = 0; i < n; ++i) a = fe_sqr(a);
    return a;
}

// The three fixed exponents (p - 2, (p + 1) / 4 and (p - 1) / 2) all start
// with 223 one bits: a^(2^223 - 1) is computed with the addition chain of
// libsecp256k1 and the tails are hard-coded. No table, no data-dependent
// indexing, so everything stays in registers.
struct fe_chain { fe8 x2, x3, x22, x223; };

GPU_FN fe_chain fe_chain_build(fe8 a) {
    fe_chain c;
    c.x2 = fe_mul(fe_sqr(a), a);
    c.x3 = fe_mul(fe_sqr(c.x2), a);
    fe8 const x6 = fe_mul(fe_sqr_n(c.x3, 3), c.x3);
    fe8 const x9 = fe_mul(fe_sqr_n(x6, 3), c.x3);
    fe8 const x11 = fe_mul(fe_sqr_n(x9, 2), c.x2);
    c.x22 = fe_mul(fe_sqr_n(x11, 11), x11);
    fe8 const x44 = fe_mul(fe_sqr_n(c.x22, 22), c.x22);
    fe8 const x88 = fe_mul(fe_sqr_n(x44, 44), x44);
    fe8 const x176 = fe_mul(fe_sqr_n(x88, 88), x88);
    fe8 const x220 = fe_mul(fe_sqr_n(x176, 44), x44);
    c.x223 = fe_mul(fe_sqr_n(x220, 3), c.x3);
    return c;
}

// a^(p - 2): tail bits 0 FFFFFC2D.
GPU_FN fe8 fe_inverse(fe8 a) {
    fe_chain const c = fe_chain_build(a);
    fe8 t = fe_mul(fe_sqr_n(c.x223, 23), c.x22);
    t = fe_mul(fe_sqr_n(t, 5), a);
    t = fe_mul(fe_sqr_n(t, 3), c.x2);
    t = fe_mul(fe_sqr_n(t, 2), a);
    return t;
}
// a^((p + 1) / 4): a square root when a is a square (the caller checks).
GPU_FN fe8 fe_sqrt_candidate(fe8 a) {
    fe_chain const c = fe_chain_build(a);
    fe8 t = fe_mul(fe_sqr_n(c.x223, 23), c.x22);
    t = fe_mul(fe_sqr_n(t, 6), c.x2);
    return fe_sqr_n(t, 2);
}
// Mask: a is a non-zero quadratic residue (Euler's criterion, a^((p - 1) / 2) == 1);
// tail bits 0 7FFFFE17.
GPU_FN u32 fe_is_square_nonzero(fe8 a) {
    fe_chain const c = fe_chain_build(a);
    fe8 t = fe_mul(fe_sqr_n(c.x223, 23), c.x22);
    t = fe_mul(fe_sqr_n(t, 5), a);
    t = fe_mul(fe_sqr_n(t, 4), c.x3);
    return fe_eq(t, fe_one());
}
GPU_FN fe8 fe_beta() {
    fe8 b;
    b.n[0] = 0x719501EEu; b.n[1] = 0xC1396C28u; b.n[2] = 0x12F58995u; b.n[3] = 0x9CF04975u;
    b.n[4] = 0xAC3434E9u; b.n[5] = 0x6E64479Eu; b.n[6] = 0x657C0710u; b.n[7] = 0x7AE96A2Bu;
    return b;
}


// ---- variable-time inverse (Bernstein-Yang safegcd, 30-bit divsteps) ----
// Verification only handles public data, so the inverse used to normalise
// the per-signature table may run in variable time: 25 batches of 30
// divsteps on the low words with a 2x2 transition matrix applied to the
// signed 30-bit limbs, as in libsecp256k1's modinv32_var. On the GPU this
// costs a sixth of the a^(p-2) addition chain.
struct fe_s30 { i32 v[9]; };
struct fe_t2x2 { i32 u, v, q, r; };

GPU_FN fe_s30 fe_s30_from_words(const GPU_THREAD u32* w) {
    fe_s30 r;
    u64 acc = 0; int bits = 0, k = 0;
    for (int i = 0; i < 8; ++i) {
        acc |= (u64)w[i] << bits; bits += 32;
        while (bits >= 30 && k < 8) { r.v[k++] = (i32)(acc & 0x3FFFFFFF); acc >>= 30; bits -= 30; }
    }
    r.v[8] = (i32)acc;
    return r;
}
GPU_FN fe_s30 fe_s30_modulus() {
    u32 w[8];
    for (int i = 0; i < 8; ++i) w[i] = fe_p_limb(i);
    return fe_s30_from_words(w);
}
GPU_FN fe8 fe_s30_to_fe(fe_s30 a) {   // a in [0, p)
    fe8 r;
    u64 acc = 0; int bits = 0, k = 0;
    for (int i = 0; i < 9; ++i) {
        acc |= (u64)(u32)a.v[i] << bits; bits += 30;
        while (bits >= 32 && k < 8) { r.n[k++] = (u32)acc; acc >>= 32; bits -= 32; }
    }
    return r;
}
GPU_FN int fe_ctz32(u32 x) {
#if defined(__CUDACC__)
    return __ffs(x) - 1;
#elif defined(__METAL_VERSION__)
    return ctz(x);
#else
    return __builtin_ctz(x);
#endif
}
// 30 divsteps on the low words of f and g; returns the new eta and the
// transition matrix scaled by 2^30.
GPU_FN i32 fe_divsteps_30_var(i32 eta, u32 f0, u32 g0, GPU_THREAD fe_t2x2* t) {
    u32 u = 1, v = 0, q = 0, r = 1;
    u32 f = f0, g = g0, m, w;
    int i = 30, limit, zeros;
    for (;;) {
        zeros = fe_ctz32(g | (0xFFFFFFFFu << i));
        g >>= zeros; u <<= zeros; v <<= zeros; eta -= zeros; i -= zeros;
        if (i == 0) break;
        if (eta < 0) {
            u32 tmp;
            eta = -eta;
            tmp = f; f = g; g = 0u - tmp;
            tmp = u; u = q; q = 0u - tmp;
            tmp = v; v = r; r = 0u - tmp;
            limit = ((int)eta + 1) > i ? i : ((int)eta + 1);
            m = (0xFFFFFFFFu >> (32 - limit)) & 255u;
            // w = -g * f^-1 mod 2^8: f is odd, so f is its own inverse mod 8
            // and two Newton steps give the inverse mod 2^12.
            u32 inv = f;
            inv *= 2u - f * inv;
            inv *= 2u - f * inv;
            w = (0u - g * inv) & m;
        } else {
            limit = ((int)eta + 1) > i ? i : ((int)eta + 1);
            m = (0xFFFFFFFFu >> (32 - limit)) & 15u;
            w = f + (((f + 1) & 4) << 1);
            w = (0u - w * g) & m;
        }
        g += f * w; q += u * w; r += v * w;
    }
    t->u = (i32)u; t->v = (i32)v; t->q = (i32)q; t->r = (i32)r;
    return eta;
}
// (d, e) = t * (d, e) / 2^30 mod m, kept in (-2m, m); inv30 = m^-1 mod 2^30.
GPU_FN void fe_update_de_30(GPU_THREAD fe_s30* d, GPU_THREAD fe_s30* e, const GPU_THREAD fe_t2x2* t, const GPU_THREAD fe_s30* modulus, u32 inv30) {
    i32 const M30 = 0x3FFFFFFF;
    i32 const u = t->u, v = t->v, q = t->q, r = t->r;
    i32 di, ei, md, me, sd, se;
    i64 cd, ce;
    sd = d->v[8] >> 31; se = e->v[8] >> 31;
    md = (u & sd) + (v & se);
    me = (q & sd) + (r & se);
    di = d->v[0]; ei = e->v[0];
    cd = (i64)u * di + (i64)v * ei;
    ce = (i64)q * di + (i64)r * ei;
    md -= (i32)((inv30 * (u32)cd + (u32)md) & (u32)M30);
    me -= (i32)((inv30 * (u32)ce + (u32)me) & (u32)M30);
    cd += (i64)modulus->v[0] * md;
    ce += (i64)modulus->v[0] * me;
    cd >>= 30; ce >>= 30;
    for (int i = 1; i < 9; ++i) {
        di = d->v[i]; ei = e->v[i];
        cd += (i64)u * di + (i64)v * ei;
        ce += (i64)q * di + (i64)r * ei;
        cd += (i64)modulus->v[i] * md;
        ce += (i64)modulus->v[i] * me;
        d->v[i - 1] = (i32)cd & M30; cd >>= 30;
        e->v[i - 1] = (i32)ce & M30; ce >>= 30;
    }
    d->v[8] = (i32)cd;
    e->v[8] = (i32)ce;
}
// (f, g) = t * (f, g) / 2^30 over the first len limbs.
GPU_FN void fe_update_fg_30_var(int len, GPU_THREAD fe_s30* f, GPU_THREAD fe_s30* g, const GPU_THREAD fe_t2x2* t) {
    i32 const M30 = 0x3FFFFFFF;
    i32 const u = t->u, v = t->v, q = t->q, r = t->r;
    i32 fi, gi;
    i64 cf, cg;
    fi = f->v[0]; gi = g->v[0];
    cf = (i64)u * fi + (i64)v * gi;
    cg = (i64)q * fi + (i64)r * gi;
    cf >>= 30; cg >>= 30;
    for (int i = 1; i < len; ++i) {
        fi = f->v[i]; gi = g->v[i];
        cf += (i64)u * fi + (i64)v * gi;
        cg += (i64)q * fi + (i64)r * gi;
        f->v[i - 1] = (i32)cf & M30; cf >>= 30;
        g->v[i - 1] = (i32)cg & M30; cg >>= 30;
    }
    f->v[len - 1] = (i32)cf;
    g->v[len - 1] = (i32)cg;
}
// Brings r from (-2p, p), times the sign of f, into [0, p).
GPU_FN void fe_s30_normalize(GPU_THREAD fe_s30* r, i32 sign, const GPU_THREAD fe_s30* modulus) {
    i32 const M30 = 0x3FFFFFFF;
    i32 cond = r->v[8] >> 31;
    for (int i = 0; i < 9; ++i) r->v[i] += modulus->v[i] & cond;
    cond = sign >> 31;
    for (int i = 0; i < 9; ++i) r->v[i] = (r->v[i] ^ cond) - cond;
    for (int i = 0; i < 8; ++i) { r->v[i + 1] += r->v[i] >> 30; r->v[i] &= M30; }
    cond = r->v[8] >> 31;
    for (int i = 0; i < 9; ++i) r->v[i] += modulus->v[i] & cond;
    for (int i = 0; i < 8; ++i) { r->v[i + 1] += r->v[i] >> 30; r->v[i] &= M30; }
}
// g^-1 mod m for g in [0, m) (0 gives 0), variable time; inv30 = m^-1 mod 2^30.
GPU_FN fe_s30 fe_s30_inverse_var(fe_s30 g, const GPU_THREAD fe_s30* modulus, u32 inv30) {
    fe_s30 d, e, f = *modulus;
    for (int i = 0; i < 9; ++i) { d.v[i] = 0; e.v[i] = 0; }
    e.v[0] = 1;
    int len = 9;
    i32 eta = -1;
    // 741 divsteps suffice for 256-bit inputs (25 batches); the loop runs
    // until g is zero, with a cap that no valid input reaches.
    for (int it = 0; it < 40; ++it) {
        fe_t2x2 t;
        eta = fe_divsteps_30_var(eta, (u32)f.v[0], (u32)g.v[0], &t);
        fe_update_de_30(&d, &e, &t, modulus, inv30);
        fe_update_fg_30_var(len, &f, &g, &t);
        if (g.v[0] == 0) {
            i32 cond = 0;
            for (int j = 1; j < len; ++j) cond |= g.v[j];
            if (cond == 0) break;
        }
        i32 const fn = f.v[len - 1], gn = g.v[len - 1];
        i32 cond = ((i32)len - 2) >> 31;
        cond |= fn ^ (fn >> 31);
        cond |= gn ^ (gn >> 31);
        if (cond == 0) { f.v[len - 2] |= (i32)((u32)fn << 30); g.v[len - 2] |= (i32)((u32)gn << 30); --len; }
    }
    fe_s30_normalize(&d, f.v[len - 1], modulus);
    return d;
}
// a^-1 in the field (0 gives 0), variable time. 0x2DDACACF = p^-1 mod 2^30.
GPU_FN fe8 fe_inverse_var(fe8 a) {
    fe_s30 const modulus = fe_s30_modulus();
    fe8 const an = fe_normalize(a);
    return fe_s30_to_fe(fe_s30_inverse_var(fe_s30_from_words(an.n), &modulus, 0x2DDACACFu));
}

// ---- scalars mod n (the group order) ----
// Plain C++: a few operations per signature (s^-1, u1, u2, the GLV split),
// next to nothing beside the point multiplication.
struct sc8 { u32 n[8]; };

GPU_FN sc8 sc_words(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {
    sc8 r; r.n[0] = a0; r.n[1] = a1; r.n[2] = a2; r.n[3] = a3; r.n[4] = a4; r.n[5] = a5; r.n[6] = a6; r.n[7] = a7; return r;
}
GPU_FN sc8 sc_order()      { return sc_words(0xD0364141u, 0xBFD25E8Cu, 0xAF48A03Bu, 0xBAAEDCE6u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu); }
GPU_FN sc8 sc_half_order() { return sc_words(0x681B20A0u, 0xDFE92F46u, 0x57A4501Du, 0x5D576E73u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu); }  // (n - 1) / 2
GPU_FN sc8 sc_r2()         { return sc_words(0x67D7D140u, 0x896CF214u, 0x0E7CF878u, 0x741496C2u, 0x5BCD07C6u, 0xE697F5E4u, 0x81C69BC5u, 0x9D671CD5u); }  // 2^512 mod n
GPU_FN sc8 sc_lambda()     { return sc_words(0x1B23BD72u, 0xDF02967Cu, 0x20816678u, 0x122E22EAu, 0x8812645Au, 0xA5261C02u, 0xC05C30E0u, 0x5363AD4Cu); }
GPU_FN sc8 sc_minus_b1()   { return sc_words(0x0ABFE4C3u, 0x6F547FA9u, 0x010E8828u, 0xE4437ED6u, 0, 0, 0, 0); }
GPU_FN sc8 sc_minus_b2()   { return sc_words(0x3DB1562Cu, 0xD765CDA8u, 0x0774346Du, 0x8A280AC5u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu); }
GPU_FN sc8 sc_g1()         { return sc_words(0x45DBB031u, 0xE893209Au, 0x71E8CA7Fu, 0x3DAA8A14u, 0x9284EB15u, 0xE86C90E4u, 0xA7D46BCDu, 0x3086D221u); }
GPU_FN sc8 sc_g2()         { return sc_words(0x8AC47F71u, 0x1571B4AEu, 0x9DF506C6u, 0x221208ACu, 0x0ABFE4C4u, 0x6F547FA9u, 0x010E8828u, 0xE4437ED6u); }
#define SC_NPRIME 0x5588B13Fu   // -n^-1 mod 2^32
#define SC_INV30  0x2A774EC1u   // n^-1 mod 2^30

GPU_FN sc8 sc_from_words(const GPU_GLOBAL u32* w) { sc8 r; for (int i = 0; i < 8; ++i) r.n[i] = w[i]; return r; }
GPU_FN u32 sc_is_zero(sc8 a) { u32 o = 0; for (int i = 0; i < 8; ++i) o |= a.n[i]; return o == 0; }
// a < b
GPU_FN u32 sc_lt(sc8 a, sc8 b) {
    for (int i = 7; i >= 0; --i) {
        if (a.n[i] != b.n[i]) return a.n[i] < b.n[i];
    }
    return 0;
}
// a - b for a >= b
GPU_FN sc8 sc_sub(sc8 a, sc8 b) {
    sc8 r;
    u64 bw = 0;
    for (int i = 0; i < 8; ++i) { u64 d = (u64)a.n[i] - b.n[i] - bw; r.n[i] = (u32)d; bw = (d >> 32) & 1u; }
    return r;
}
// a mod n for a < 2^256 (< 2n)
GPU_FN sc8 sc_reduce(sc8 a) { sc8 const n = sc_order(); return sc_lt(a, n) ? a : sc_sub(a, n); }
GPU_FN sc8 sc_add_mod(sc8 a, sc8 b) {
    sc8 r;
    u64 c = 0;
    for (int i = 0; i < 8; ++i) { u64 x = (u64)a.n[i] + b.n[i] + c; r.n[i] = (u32)x; c = x >> 32; }
    sc8 const n = sc_order();
    return (c != 0 || !sc_lt(r, n)) ? sc_sub(r, n) : r;
}
GPU_FN sc8 sc_neg_mod(sc8 a) { return sc_is_zero(a) ? a : sc_sub(sc_order(), a); }
// Full 512-bit product.
GPU_FN void sc_mul_512(sc8 a, sc8 b, GPU_THREAD u32* t) {
    for (int i = 0; i < 16; ++i) t[i] = 0;
    for (int i = 0; i < 8; ++i) {
        u64 c = 0;
        for (int j = 0; j < 8; ++j) { u64 x = (u64)a.n[i] * b.n[j] + t[i + j] + c; t[i + j] = (u32)x; c = x >> 32; }
        t[i + 8] = (u32)c;
    }
}
// a * b * 2^-256 mod n (Montgomery, CIOS).
GPU_FN sc8 sc_mont_mul(sc8 a, sc8 b) {
    sc8 const n = sc_order();
    u32 t[10];
    for (int i = 0; i < 10; ++i) t[i] = 0;
    for (int i = 0; i < 8; ++i) {
        u64 c = 0, x;
        for (int j = 0; j < 8; ++j) { x = (u64)a.n[j] * b.n[i] + t[j] + c; t[j] = (u32)x; c = x >> 32; }
        x = (u64)t[8] + c; t[8] = (u32)x; t[9] = (u32)(x >> 32);
        u32 const m = t[0] * SC_NPRIME;
        c = ((u64)m * n.n[0] + t[0]) >> 32;
        for (int j = 1; j < 8; ++j) { x = (u64)m * n.n[j] + t[j] + c; t[j - 1] = (u32)x; c = x >> 32; }
        x = (u64)t[8] + c; t[7] = (u32)x; c = x >> 32;
        t[8] = t[9] + (u32)c;
    }
    sc8 r;
    for (int i = 0; i < 8; ++i) r.n[i] = t[i];
    return (t[8] != 0 || !sc_lt(r, n)) ? sc_sub(r, n) : r;
}
GPU_FN sc8 sc_mul_mod(sc8 a, sc8 b) { return sc_mont_mul(sc_mont_mul(a, b), sc_r2()); }
// round(a * b / 2^384), as secp256k1_scalar_mul_shift_var.
GPU_FN sc8 sc_mul_shift_384(sc8 a, sc8 b) {
    u32 t[16];
    sc_mul_512(a, b, t);
    sc8 r;
    u64 c = (t[11] >> 31) & 1u;
    for (int i = 0; i < 4; ++i) { u64 x = (u64)t[12 + i] + c; r.n[i] = (u32)x; c = x >> 32; }
    for (int i = 4; i < 8; ++i) r.n[i] = 0;
    return r;
}
// a^-1 mod n for a in [1, n), variable time.
GPU_FN sc8 sc_inverse_var(sc8 a) {
    fe_s30 const modulus = fe_s30_from_words(sc_order().n);
    fe_s30 const d = fe_s30_inverse_var(fe_s30_from_words(a.n), &modulus, SC_INV30);
    fe8 const f = fe_s30_to_fe(d);
    sc8 r;
    for (int i = 0; i < 8; ++i) r.n[i] = f.n[i];
    return r;
}
// k = (+-k1) + lambda * (+-k2) mod n with |k1|, |k2| < 2^128 (secp256k1_scalar_split_lambda).
struct sc_split { u32 k1[4]; u32 k2[4]; u32 k1neg; u32 k2neg; };
GPU_FN sc_split sc_split_lambda(sc8 k) {
    sc8 c1 = sc_mul_shift_384(k, sc_g1());
    sc8 c2 = sc_mul_shift_384(k, sc_g2());
    c1 = sc_mul_mod(c1, sc_minus_b1());
    c2 = sc_mul_mod(c2, sc_minus_b2());
    sc8 const r2 = sc_add_mod(c1, c2);
    sc8 r1 = sc_mul_mod(r2, sc_lambda());
    r1 = sc_neg_mod(r1);
    r1 = sc_add_mod(r1, k);
    sc_split s;
    s.k1neg = (r1.n[4] | r1.n[5] | r1.n[6] | r1.n[7]) != 0;
    s.k2neg = (r2.n[4] | r2.n[5] | r2.n[6] | r2.n[7]) != 0;
    sc8 const a1 = s.k1neg ? sc_neg_mod(r1) : r1;
    sc8 const a2 = s.k2neg ? sc_neg_mod(r2) : r2;
    for (int i = 0; i < 4; ++i) { s.k1[i] = a1.n[i]; s.k2[i] = a2.n[i]; }
    return s;
}

// ---- group law (Jacobian, infinity as a mask; same formulas as group.hpp) ----

GPU_FN gej8 gej_infinity() { gej8 r; r.x = fe_zero(); r.y = fe_zero(); r.z = fe_zero(); r.inf = 0xFFFFFFFFu; return r; }
GPU_FN gej8 gej_from_ge(ge8 a) { gej8 r; r.x = a.x; r.y = a.y; r.z = fe_one(); r.inf = 0; return r; }
GPU_FN gej8 gej_select(u32 m, gej8 a, gej8 b) {
    gej8 r;
    r.x = fe_select(m, a.x, b.x); r.y = fe_select(m, a.y, b.y); r.z = fe_select(m, a.z, b.z);
    r.inf = (a.inf & m) | (b.inf & ~m);
    return r;
}
GPU_FN ge8 ge_cneg(u32 m, ge8 a) { ge8 r; r.x = a.x; r.y = fe_cneg(m, a.y); return r; }

// dbl-2009-l: 2M + 5S.
GPU_FN gej8 gej_dbl(gej8 a) {
    fe8 const A = fe_sqr(a.x);
    fe8 const B = fe_sqr(a.y);
    fe8 const C = fe_sqr(B);
    fe8 D = fe_add(a.x, B);
    D = fe_sqr(D);
    D = fe_sub(D, A);
    D = fe_sub(D, C);
    D = fe_mul_small(D, 2);
    fe8 const E = fe_mul_small(A, 3);
    fe8 const F = fe_sqr(E);
    gej8 r;
    r.x = fe_sub(F, fe_mul_small(D, 2));
    r.y = fe_sub(fe_mul(E, fe_sub(D, r.x)), fe_mul_small(C, 8));
    r.z = fe_mul_small(fe_mul(a.y, a.z), 2);
    r.inf = a.inf;
    return r;
}

// Complete mixed addition (port of secp256k1_gej_add_ge).
GPU_FN gej8 gej_add_ge(gej8 a, ge8 b) {
    fe8 const zz = fe_sqr(a.z);
    fe8 const u1 = a.x;
    fe8 const u2 = fe_mul(b.x, zz);
    fe8 const s1 = a.y;
    fe8 s2 = fe_mul(b.y, zz);
    s2 = fe_mul(s2, a.z);
    fe8 t = fe_add(u1, u2);
    fe8 const m = fe_add(s1, s2);
    fe8 rr = fe_sqr(t);
    fe8 m_alt = fe_neg(u2);
    fe8 const tt = fe_mul(u1, m_alt);
    rr = fe_add(rr, tt);
    u32 const degenerate = fe_is_zero(m);
    fe8 rr_alt = fe_mul_small(s1, 2);
    m_alt = fe_add(m_alt, u1);
    rr_alt = fe_select(degenerate, rr_alt, rr);
    m_alt = fe_select(degenerate, m_alt, m);
    fe8 n = fe_sqr(m_alt);
    fe8 q = fe_neg(t);
    q = fe_mul(q, n);
    n = fe_sqr(n);
    n = fe_select(degenerate, m, n);
    t = fe_sqr(rr_alt);
    gej8 r;
    r.z = fe_mul(a.z, m_alt);
    t = fe_add(t, q);
    r.x = t;
    t = fe_mul_small(t, 2);
    t = fe_add(t, q);
    t = fe_mul(t, rr_alt);
    t = fe_add(t, n);
    r.y = fe_neg(t);
    r.y = fe_half(r.y);
    r.x = fe_select(a.inf, b.x, r.x);
    r.y = fe_select(a.inf, b.y, r.y);
    r.z = fe_select(a.inf, fe_one(), r.z);
    r.inf = fe_is_zero(r.z);
    return r;
}

// ---- per-thread table of d * Q, d = 1..15, affine, plus beta * x ----

struct qtab { fe8 x[15]; fe8 y[15]; fe8 bx[15]; };

GPU_FN void qtab_build(ge8 q, GPU_THREAD qtab* t) {
    // Jacobian d * Q, kept in the table itself (Z in the beta * x slot).
    gej8 acc = gej_from_ge(q);
    t->x[0] = acc.x; t->y[0] = acc.y; t->bx[0] = acc.z;
    acc = gej_dbl(acc);
    t->x[1] = acc.x; t->y[1] = acc.y; t->bx[1] = acc.z;
    GPU_NO_UNROLL
    for (int d = 3; d <= 15; ++d) {
        acc = gej_add_ge(acc, q);
        t->x[d - 1] = acc.x; t->y[d - 1] = acc.y; t->bx[d - 1] = acc.z;
    }
    // Montgomery's trick: one inversion for the 15 Z coordinates.
    fe8 prefix[15];
    prefix[0] = t->bx[0];
    for (int i = 1; i < 15; ++i) prefix[i] = fe_mul(prefix[i - 1], t->bx[i]);
    fe8 inv = fe_inverse_var(prefix[14]);
    fe8 const beta = fe_beta();
    GPU_NO_UNROLL
    for (int i = 14; i >= 0; --i) {
        fe8 const zi = (i == 0) ? inv : fe_mul(inv, prefix[i - 1]);
        if (i != 0) inv = fe_mul(inv, t->bx[i]);
        fe8 const zi2 = fe_sqr(zi);
        t->x[i] = fe_mul(t->x[i], zi2);
        t->y[i] = fe_mul(t->y[i], fe_mul(zi2, zi));
        t->bx[i] = fe_mul(beta, t->x[i]);
    }
}

GPU_FN ge8 qtab_lookup(const GPU_THREAD qtab* t, u32 d, u32 lambda, u32 neg_y) {
    u32 const i = d - 1;
    ge8 p;
    p.x = lambda ? t->bx[i] : t->x[i];
    p.y = t->y[i];
    if (lambda) p.y = fe_cneg(neg_y, p.y);
    return p;
}

GPU_FN ge8 gtab_lookup(GPU_GTAB g, u32 d) {
    ge8 p;
    for (int i = 0; i < 8; ++i) p.x.n[i] = g[i * GPU_G_ENTRIES + d];
    for (int i = 0; i < 8; ++i) p.y.n[i] = g[(8 + i) * GPU_G_ENTRIES + d];
    return p;
}

GPU_FN u32 gpu_nibble(const GPU_THREAD u32* a, int j) { return (a[j >> 3] >> (4 * (j & 7))) & 15u; }
GPU_FN void gpu_copy_words(GPU_THREAD u32* dst, const GPU_GLOBAL u32* src, int n) { for (int i = 0; i < n; ++i) dst[i] = src[i]; }
GPU_FN u32 gpu_nonzero_digit(u32 d) { return d | (d == 0 ? 1u : 0u); }

// acc += p unless d == 0 (branch-free so that a warp stays uniform).
GPU_FN gej8 gpu_accumulate(gej8 acc, u32 d, ge8 p) {
    gej8 const sum = gej_add_ge(acc, p);
    return gej_select(gpu_mask(d == 0), acc, sum);
}

// ---- SHA-256, for the BCH Schnorr challenge ----
// Written out round by round with the constants inline: the schedule stays
// in registers and the zero words of the padding block fold away.
#define SHA_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA_R(a, b, c, d, e, f, g, h, i, k) { \
    if ((i) >= 16) w[(i) & 15] += (SHA_ROTR(w[((i) + 14) & 15], 17) ^ SHA_ROTR(w[((i) + 14) & 15], 19) ^ (w[((i) + 14) & 15] >> 10)) + \
                                  w[((i) + 9) & 15] + (SHA_ROTR(w[((i) + 1) & 15], 7) ^ SHA_ROTR(w[((i) + 1) & 15], 18) ^ (w[((i) + 1) & 15] >> 3)); \
    u32 const t1 = h + (SHA_ROTR(e, 6) ^ SHA_ROTR(e, 11) ^ SHA_ROTR(e, 25)) + ((e & f) ^ (~e & g)) + (k) + w[(i) & 15]; \
    d += t1; \
    h = t1 + (SHA_ROTR(a, 2) ^ SHA_ROTR(a, 13) ^ SHA_ROTR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c)); }
#define SHA_R8(i, k0, k1, k2, k3, k4, k5, k6, k7) \
    SHA_R(a, b, c, d, e, f, g, h, (i), k0) SHA_R(h, a, b, c, d, e, f, g, (i) + 1, k1) \
    SHA_R(g, h, a, b, c, d, e, f, (i) + 2, k2) SHA_R(f, g, h, a, b, c, d, e, (i) + 3, k3) \
    SHA_R(e, f, g, h, a, b, c, d, (i) + 4, k4) SHA_R(d, e, f, g, h, a, b, c, (i) + 5, k5) \
    SHA_R(c, d, e, f, g, h, a, b, (i) + 6, k6) SHA_R(b, c, d, e, f, g, h, a, (i) + 7, k7)

// One compression of the 16 big-endian words w (overwritten) into st.
GPU_FN void sha256_compress(GPU_THREAD u32* st, GPU_THREAD u32* w) {
    u32 a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
    SHA_R8( 0, 0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u)
    SHA_R8( 8, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u)
    SHA_R8(16, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau)
    SHA_R8(24, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u)
    SHA_R8(32, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u)
    SHA_R8(40, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u)
    SHA_R8(48, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u)
    SHA_R8(56, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u)
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}
#undef SHA_R8
#undef SHA_R
#undef SHA_ROTR

// e = SHA256(r || compressed key || message) as a scalar below 2^256, from
// the input words (word i holds big-endian bytes 28 - 4i .. 31 - 4i, so the
// 32-byte fields are already SHA-256 words in reverse order). The key prefix
// byte shifts x and the message by one byte; the 97 bytes take two blocks.
GPU_FN sc8 gpu_schnorr_challenge(const GPU_GLOBAL gpu_raw_input* in, u32 key_odd) {
    u32 st[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    u32 w[16];
    for (int i = 0; i < 8; ++i) w[i] = in->r[7 - i];
    w[8] = ((key_odd ? 0x03u : 0x02u) << 24) | (in->qx[7] >> 8);
    for (int j = 1; j < 8; ++j) w[8 + j] = (in->qx[8 - j] << 24) | (in->qx[7 - j] >> 8);
    sha256_compress(st, w);
    w[0] = (in->qx[0] << 24) | (in->z[7] >> 8);
    for (int j = 1; j < 8; ++j) w[j] = (in->z[8 - j] << 24) | (in->z[7 - j] >> 8);
    w[8] = (in->z[0] << 24) | 0x00800000u;   // last message byte, then the 0x80 pad byte
    for (int j = 9; j < 15; ++j) w[j] = 0;
    w[15] = 97 * 8;                           // message length in bits
    sha256_compress(st, w);
    sc8 e;
    for (int i = 0; i < 8; ++i) e.n[i] = st[7 - i];
    return e;
}

// The verification in three pieces: checks and scalar derivations, the
// ladder, the verdict.
struct gpu_prep {
    u32 ok;          // structural and range checks passed (else the verdict is 0)
    u32 valid;       // mask accumulated from the key checks
    fe8 x, y;        // public key
    sc8 u1;          // G scalar
    sc_split sp;     // GLV split of the Q scalar
    fe8 rn;          // r + n (ECDSA, when use_rn)
    u32 use_rn;
};

GPU_FN gpu_prep gpu_prepare(const GPU_GLOBAL gpu_raw_input* in, u32 schnorr) {
    gpu_prep pr;
    pr.ok = 0; pr.valid = 0; pr.use_rn = 0;
    pr.x = fe_zero(); pr.y = fe_zero(); pr.rn = fe_zero();
    for (int i = 0; i < 8; ++i) pr.u1.n[i] = 0;
    for (int i = 0; i < 4; ++i) { pr.sp.k1[i] = 0; pr.sp.k2[i] = 0; }
    pr.sp.k1neg = 0; pr.sp.k2neg = 0;
    u32 const flags = in->flags;
    if ((flags & GPU_FLAG_OK) == 0) return pr;

    // ---- scalars: u1 (G) and u2 (Q), the latter GLV-split ----
    sc8 const n = sc_order();
    sc8 const s = sc_from_words(in->s);
    sc8 u2;
    if (schnorr) {
        // r is a field element here; e = SHA256(r || compressed key || m) mod n; R = s G - e Q.
        if (!fe_lt_p(fe_from_words(in->r)) || !sc_lt(s, n)) return pr;
        pr.u1 = s;
        u2 = sc_neg_mod(sc_reduce(gpu_schnorr_challenge(in, flags & GPU_FLAG_PARITY_ODD)));
    } else {
        sc8 const r = sc_from_words(in->r);
        if (sc_is_zero(r) || !sc_lt(r, n) || sc_is_zero(s) || !sc_lt(s, n)) return pr;
        if (sc_lt(sc_half_order(), s)) return pr;   // high-S, rejected like secp256k1_ecdsa_verify
        sc8 const w = sc_inverse_var(s);
        pr.u1 = sc_mul_mod(sc_reduce(sc_from_words(in->z)), w);
        u2 = sc_mul_mod(r, w);
        // r + n is a second candidate for R.x when it is still below p.
        u64 c = 0;
        for (int i = 0; i < 8; ++i) { u64 x = (u64)r.n[i] + n.n[i] + c; pr.rn.n[i] = (u32)x; c = x >> 32; }
        pr.use_rn = (c == 0) & fe_lt_p(pr.rn);
    }
    pr.sp = sc_split_lambda(u2);

    // ---- public key: range, optional decompression, on-curve check ----
    pr.x = fe_from_words(in->qx);
    pr.y = fe_from_words(in->qy);
    pr.valid = fe_lt_p(pr.x) ? 0xFFFFFFFFu : 0u;
    fe8 const rhs = fe_add(fe_mul(fe_sqr(pr.x), pr.x), fe_small(7));
    if (flags & GPU_FLAG_DECOMPRESS) {
        fe8 const yc = fe_sqrt_candidate(rhs);
        pr.valid &= fe_eq(fe_sqr(yc), rhs);
        u32 const parity_odd = gpu_mask(flags & GPU_FLAG_PARITY_ODD);
        pr.y = fe_cneg(fe_is_odd(yc) ^ parity_odd, yc);
    } else {
        pr.valid &= fe_lt_p(pr.y) ? 0xFFFFFFFFu : 0u;
        pr.valid &= fe_eq(fe_sqr(pr.y), rhs);
    }
    pr.ok = 1;
    return pr;
}

// u1 G + u2 Q with the three multiplications interleaved (one thread).
// One copy of the doubling and one of the addition in the code: a step
// written out is 17k instructions, more than the instruction cache holds,
// and every operation would pay for its own fetch.
GPU_FN gej8 gpu_ladder(const GPU_THREAD gpu_prep* pr, GPU_GTAB g) {
    u32 const k1neg = gpu_mask(pr->sp.k1neg), k2neg = gpu_mask(pr->sp.k2neg);
    ge8 q; q.x = pr->x; q.y = pr->y;
    qtab qt;
    qtab_build(ge_cneg(k1neg, q), &qt);
    u32 const lambda_neg = k1neg ^ k2neg;
    gej8 acc = gej_infinity();
    GPU_NO_UNROLL
    for (int j = 31; j >= 0; --j) {
        if (j != 31) {
            GPU_NO_UNROLL
            for (int t = 0; t < 4; ++t) acc = gej_dbl(acc);
        }
        GPU_NO_UNROLL
        for (int which = 0; which < 3; ++which) {
            u32 d;
            ge8 p;
            if (which == 0) { d = gpu_nibble(pr->sp.k1, j); p = qtab_lookup(&qt, gpu_nonzero_digit(d), 0, lambda_neg); }
            else if (which == 1) { d = gpu_nibble(pr->sp.k2, j); p = qtab_lookup(&qt, gpu_nonzero_digit(d), 1, lambda_neg); }
            else { d = gpu_nibble(pr->u1.n, j) | (gpu_nibble(pr->u1.n, j + 32) << 4); p = gtab_lookup(g, gpu_nonzero_digit(d)); }
            acc = gpu_accumulate(acc, d, p);
        }
    }
    return acc;
}

// The verdict from the point u1 G + u2 Q.
GPU_FN u32 gpu_finish(const GPU_GLOBAL gpu_raw_input* in, const GPU_THREAD gpu_prep* pr, gej8 acc, u32 schnorr) {
    if (!pr->ok) return 0;
    u32 valid = pr->valid & ~acc.inf;
    fe8 const z2 = fe_sqr(acc.z);
    fe8 const r = fe_from_words(in->r);
    u32 x_ok = fe_eq(acc.x, fe_mul(r, z2));
    if (schnorr) {
        valid &= x_ok & fe_is_square_nonzero(fe_mul(acc.y, acc.z));
    } else {
        if (pr->use_rn) x_ok |= fe_eq(acc.x, fe_mul(pr->rn, z2));
        valid &= x_ok;
    }
    return valid & 1u;
}

// Returns 1 if the signature verifies, 0 otherwise (one thread).
GPU_FN u32 gpu_verify_one(const GPU_GLOBAL gpu_raw_input* in, GPU_GTAB g, u32 schnorr) {
    gpu_prep const pr = gpu_prepare(in, schnorr);
    if (!pr.ok) return 0;
    return gpu_finish(in, &pr, gpu_ladder(&pr, g), schnorr);
}

// Field self-test, run on the device by the tests against the host results.
// Inputs and outputs are 8 little-endian 32-bit words (canonical).
struct gpu_field_test_in { u32 a[8]; u32 b[8]; };
struct gpu_field_test_out { u32 mul[8]; u32 sqr[8]; u32 add[8]; u32 sub[8]; u32 hlf[8]; u32 mul3[8]; u32 inv[8]; u32 sqrt[8]; u32 invv[8]; u32 is_square; u32 pad[7]; };

GPU_FN void gpu_field_test_one(const GPU_GLOBAL gpu_field_test_in* in, GPU_GLOBAL gpu_field_test_out* out) {
    fe8 const a = fe_from_words(in->a);
    fe8 const b = fe_from_words(in->b);
    fe_to_words(fe_mul(a, b), out->mul);
    fe_to_words(fe_sqr(a), out->sqr);
    fe_to_words(fe_add(a, b), out->add);
    fe_to_words(fe_sub(a, b), out->sub);
    fe_to_words(fe_half(a), out->hlf);
    fe_to_words(fe_mul_small(a, 3), out->mul3);
    fe_to_words(fe_inverse(a), out->inv);
    fe_to_words(fe_sqrt_candidate(a), out->sqrt);
    fe_to_words(fe_inverse_var(a), out->invv);
    out->is_square = fe_is_square_nonzero(a);
    for (int i = 0; i < 7; ++i) out->pad[i] = 0;
}

#endif
