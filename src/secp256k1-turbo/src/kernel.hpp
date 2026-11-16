// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Lane-parallel kernel: for every lane computes P = u1 * G + u2 * Q with
// u2 = (+-k1) + lambda * (+-k2) and applies the ECDSA or BCH Schnorr final
// check. Data crosses the lane boundary only through lane_batch, a plain
// limb-major / lane-minor struct of 8 lanes that both backends consume.

#ifndef KTH_SECP256K1_TURBO_KERNEL_HPP
#define KTH_SECP256K1_TURBO_KERNEL_HPP

#include <cstddef>
#include <cstdint>

#include "g_table.hpp"
#include "group.hpp"
#include "scalar256.hpp"

namespace secp256k1_turbo {

constexpr std::size_t max_lanes = 8;

enum class check_mode { ecdsa, schnorr_bch };

struct lane_batch {
    std::uint64_t qx[5][max_lanes];       // public key x, 5x52 limbs (< p)
    std::uint64_t qy[5][max_lanes];       // public key y when not decompressing
    std::uint64_t decompress[max_lanes];  // mask: derive y from x
    std::uint64_t parity_odd[max_lanes];  // mask: requested/declared parity of y
    std::uint64_t u1d[32][max_lanes];     // 8-bit comb digits of the G scalar: nibble j | nibble (j+32) << 4
    std::uint64_t k1d[32][max_lanes];     // 4-bit digits of |k1|
    std::uint64_t k2d[32][max_lanes];     // 4-bit digits of |k2|
    std::uint64_t k1neg[max_lanes];       // masks
    std::uint64_t k2neg[max_lanes];
    std::uint64_t r[5][max_lanes];        // r as a field element (< p)
    std::uint64_t rn[5][max_lanes];       // r + n (ECDSA; valid when use_rn)
    std::uint64_t use_rn[max_lanes];      // mask
    std::uint64_t valid[max_lanes];       // output: 0 / ~0
    // Diagnostics consumed by the tests: X and Z of u1*G + u2*Q (Jacobian) and
    // the on-curve, infinity, x-match and y-is-square masks.
    std::uint64_t px[5][max_lanes];
    std::uint64_t pz[5][max_lanes];
    std::uint64_t dbg[4][max_lanes];
};

using kernel_fn = void (*)(lane_batch&, check_mode);

// Lays out the scalars of one lane: u1 (G) as 32 two-tooth comb digits, u2
// (Q) GLV-split into two signed 128-bit halves of 32 digits each.
inline void lane_set_scalars(lane_batch& b, std::size_t lane, u256 const& u1, u256 const& u2) {
    for (int j = 0; j < 32; ++j) b.u1d[j][lane] = sn::nibble(u1, j) | (sn::nibble(u1, j + 32) << 4);
    sn::split_result const sp = sn::split_lambda(u2);
    for (int j = 0; j < 32; ++j) {
        b.k1d[j][lane] = sn::nibble(sp.k1, j);
        b.k2d[j][lane] = sn::nibble(sp.k2, j);
    }
    b.k1neg[lane] = sp.k1neg ? ~std::uint64_t(0) : 0;
    b.k2neg[lane] = sp.k2neg ? ~std::uint64_t(0) : 0;
}

void kernel_scalar(lane_batch& b, check_mode mode);
#if defined(KTH_SECP256K1_TURBO_HAVE_AVX512IFMA)
void kernel_avx512ifma(lane_batch& b, check_mode mode);
#endif

template <class V>
struct ecmult {
    using Fd = field<V>;
    using F = fe<V>;
    using Gr = group<V>;
    static constexpr std::size_t L = V::lanes;
    static constexpr std::size_t q_entry_u64 = 15 * L;  // x[5], y[5], beta*x[5], lane-minor
    static constexpr int LOG2L = (L == 8) ? 3 : (L == 4) ? 2 : (L == 2) ? 1 : 0;
    static_assert((std::size_t(1) << LOG2L) == L, "lane count must be a power of two");

    struct q_table { std::uint64_t data[16 * q_entry_u64]; };

    static V lane_ids() {
        std::uint64_t ids[L];
        for (std::size_t i = 0; i < L; ++i) ids[i] = i;
        return V::load(ids);
    }

    static V any_mask(V m) { return m; }
    static bool any(V m) {
        for (std::size_t i = 0; i < L; ++i) if (m.lane(i)) return true;
        return false;
    }

    // d * Q for d in 1..15 (Q already sign-adjusted), affine, plus beta * x.
    static void build_q_table(ge<V> const& q, q_table& t) {
        gej<V> jac[15];
        ge<V> aff[15];
        F scratch[15];
        jac[0] = Gr::from_ge(q);
        jac[1] = Gr::dbl(jac[0]);
        for (int d = 3; d <= 15; ++d) jac[d - 1] = Gr::add_ge(jac[d - 2], q);
        Gr::batch_to_affine(jac, aff, 15, scratch);
        F const beta = Fd::beta();
        for (int d = 1; d <= 15; ++d) {
            std::uint64_t* e = t.data + d * q_entry_u64;
            F const bx = Fd::mul(beta, aff[d - 1].x);
            Fd::store(aff[d - 1].x, e, L);
            Fd::store(aff[d - 1].y, e + 5 * L, L);
            Fd::store(bx, e + 10 * L, L);
        }
    }

    // Entry d (per lane) of the Q table; lambda selects (beta*x, +-y).
    static ge<V> lookup_q(q_table const& t, V d, bool lambda, V ids, V neg_y) {
        V const dl = shl<LOG2L>(d);
        V idx = shl<4>(dl) - dl + ids;  // d * 15 * L + lane
        ge<V> p;
        std::uint64_t const xoff = lambda ? 10 * L : 0;
        for (int i = 0; i < 5; ++i) p.x.n[i] = gather(t.data, idx + V::set1(xoff + i * L));
        for (int i = 0; i < 5; ++i) p.y.n[i] = gather(t.data, idx + V::set1(5 * L + i * L));
        if (lambda) p.y = Fd::cneg(neg_y, p.y);
        return p;
    }

    // Entry d (per lane, 1..255) of the shared G comb table.
    static ge<V> lookup_g(g_table const& g, V d) {
        V const idx = shl<3>(d) + shl<1>(d);  // d * 10
        std::uint64_t const* base = &g.t[0][0];
        ge<V> p;
        for (int i = 0; i < 5; ++i) p.x.n[i] = gather(base, idx + V::set1(i));
        for (int i = 0; i < 5; ++i) p.y.n[i] = gather(base, idx + V::set1(5 + i));
        return p;
    }

    // acc += P where digit d selected P; lanes with d == 0 keep acc.
    static void accumulate(gej<V>& acc, V d, ge<V> const& p) {
        gej<V> const sum = Gr::add_ge(acc, p);
        acc = Gr::select(cmpeq(d, V::zero()), acc, sum);
    }
    static V nonzero_digit(V d) { return blend(cmpeq(d, V::zero()), V::set1(1), d); }

    static void run(lane_batch& b, std::size_t off, check_mode mode, g_table const& gt) {
        constexpr std::size_t S = max_lanes;  // stride of the limb-major arrays
        V const ids = lane_ids();

        // Public key: optional decompression and on-curve check.
        F const x = Fd::load(&b.qx[0][off], S);
        F rhs = Fd::mul(Fd::sqr(x), x);
        { F seven = Fd::zero(); seven.n[0] = V::set1(7); rhs = Fd::add(rhs, seven); }
        V const decompress = V::load(&b.decompress[off]);
        V const parity_odd = V::load(&b.parity_odd[off]);
        F y = Fd::load(&b.qy[0][off], S);
        V valid = Fd::eq(Fd::sqr(y), rhs);
        if (any(decompress)) {
            F yc = Fd::sqrt_candidate(rhs);
            V const on_curve = Fd::eq(Fd::sqr(yc), rhs);
            yc = Fd::cneg(Fd::is_odd(yc) ^ parity_odd, yc);
            y = Fd::select(decompress, yc, y);
            valid = blend(decompress, on_curve, valid);
        }
        ge<V> q; q.x = x; q.y = y;

        V const k1neg = V::load(&b.k1neg[off]);
        V const k2neg = V::load(&b.k2neg[off]);
        q_table qt;
        build_q_table(Gr::cneg(k1neg, q), qt);
        V const lambda_neg = k1neg ^ k2neg;

        gej<V> acc = Gr::infinity();
        for (int j = 31; j >= 0; --j) {
            if (j != 31) { acc = Gr::dbl(acc); acc = Gr::dbl(acc); acc = Gr::dbl(acc); acc = Gr::dbl(acc); }
            V const d1 = V::load(&b.k1d[j][off]);
            accumulate(acc, d1, lookup_q(qt, nonzero_digit(d1), false, ids, lambda_neg));
            V const d2 = V::load(&b.k2d[j][off]);
            accumulate(acc, d2, lookup_q(qt, nonzero_digit(d2), true, ids, lambda_neg));
            V const g = V::load(&b.u1d[j][off]);
            accumulate(acc, g, lookup_g(gt, nonzero_digit(g)));
        }

        Fd::store(acc.x, &b.px[0][off], S);
        Fd::store(acc.z, &b.pz[0][off], S);
        valid.store(&b.dbg[0][off]);
        acc.inf.store(&b.dbg[1][off]);
        valid = valid & ~acc.inf;
        F const z2 = Fd::sqr(acc.z);
        F const r = Fd::load(&b.r[0][off], S);
        V x_ok = Fd::eq(acc.x, Fd::mul(r, z2));
        if (mode == check_mode::ecdsa) {
            V const use_rn = V::load(&b.use_rn[off]);
            if (any(use_rn)) {
                F const rn = Fd::load(&b.rn[0][off], S);
                x_ok = x_ok | (use_rn & Fd::eq(acc.x, Fd::mul(rn, z2)));
            }
            valid = valid & x_ok;
            V::zero().store(&b.dbg[3][off]);
        } else {
            V const sq = Fd::is_square_nonzero(Fd::mul(acc.y, acc.z));
            sq.store(&b.dbg[3][off]);
            valid = valid & x_ok & sq;
        }
        x_ok.store(&b.dbg[2][off]);
        valid.store(&b.valid[off]);
    }

    static void run_all(lane_batch& b, check_mode mode) {
        g_table const& gt = get_g_table();
        for (std::size_t off = 0; off < max_lanes; off += L) run(b, off, mode, gt);
    }
};

} // namespace secp256k1_turbo

#endif
