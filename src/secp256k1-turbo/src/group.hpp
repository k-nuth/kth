// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Group law on secp256k1 in Jacobian coordinates, one point per lane. Every
// formula is branch-free across lanes: the point at infinity is a mask, and
// the addition is the complete (unified) formula from libsecp256k1's
// secp256k1_gej_add_ge, which handles a == b, a == -b and a == infinity.

#ifndef KTH_SECP256K1_TURBO_GROUP_HPP
#define KTH_SECP256K1_TURBO_GROUP_HPP

#include "fe52.hpp"

namespace secp256k1_turbo {

template <class V>
struct ge { fe<V> x, y; };  // affine, never infinity

template <class V>
struct gej { fe<V> x, y, z; V inf; };  // inf: all-ones mask where the lane is infinity

template <class V>
struct group {
    using Fd = field<V>;
    using F = fe<V>;

    static gej<V> infinity() {
        gej<V> r; r.x = Fd::zero(); r.y = Fd::zero(); r.z = Fd::zero(); r.inf = ~V::zero(); return r;
    }
    static gej<V> from_ge(ge<V> const& a) {
        gej<V> r; r.x = a.x; r.y = a.y; r.z = Fd::one(); r.inf = V::zero(); return r;
    }
    static gej<V> select(V m, gej<V> const& a, gej<V> const& b) {
        gej<V> r;
        r.x = Fd::select(m, a.x, b.x); r.y = Fd::select(m, a.y, b.y); r.z = Fd::select(m, a.z, b.z);
        r.inf = blend(m, a.inf, b.inf);
        return r;
    }
    static ge<V> select(V m, ge<V> const& a, ge<V> const& b) {
        ge<V> r; r.x = Fd::select(m, a.x, b.x); r.y = Fd::select(m, a.y, b.y); return r;
    }
    static ge<V> neg(ge<V> const& a) { ge<V> r; r.x = a.x; r.y = Fd::neg(a.y); return r; }
    static ge<V> cneg(V m, ge<V> const& a) { ge<V> r; r.x = a.x; r.y = Fd::cneg(m, a.y); return r; }

    // dbl-2009-l (a = 0): 2M + 5S. Infinity propagates through the mask (Z stays 0).
    static gej<V> dbl(gej<V> const& a) {
        F const A = Fd::sqr(a.x);
        F const B = Fd::sqr(a.y);
        F const C = Fd::sqr(B);
        F D = Fd::add(a.x, B);
        D = Fd::sqr(D);
        D = Fd::sub(D, A);
        D = Fd::sub(D, C);
        D = Fd::template mul_int<2>(D);
        F const E = Fd::template mul_int<3>(A);
        F const Fq = Fd::sqr(E);
        gej<V> r;
        r.x = Fd::sub(Fq, Fd::template mul_int<2>(D));
        r.y = Fd::sub(Fd::mul(E, Fd::sub(D, r.x)), Fd::template mul_int<8>(C));
        r.z = Fd::template mul_int<2>(Fd::mul(a.y, a.z));
        r.inf = a.inf;
        return r;
    }

    // r = a + b, b affine (never infinity). Complete: port of secp256k1_gej_add_ge.
    static gej<V> add_ge(gej<V> const& a, ge<V> const& b) {
        F const zz = Fd::sqr(a.z);
        F const u1 = a.x;
        F const u2 = Fd::mul(b.x, zz);
        F const s1 = a.y;
        F s2 = Fd::mul(b.y, zz);
        s2 = Fd::mul(s2, a.z);
        F t = Fd::add(u1, u2);
        F const m = Fd::add(s1, s2);
        F rr = Fd::sqr(t);
        F m_alt = Fd::neg(u2);
        F const tt = Fd::mul(u1, m_alt);
        rr = Fd::add(rr, tt);
        V const degenerate = Fd::is_zero(m);
        F rr_alt = Fd::template mul_int<2>(s1);
        m_alt = Fd::add(m_alt, u1);
        rr_alt = Fd::select(degenerate, rr_alt, rr);
        m_alt = Fd::select(degenerate, m_alt, m);
        F n = Fd::sqr(m_alt);
        F q = Fd::neg(t);
        q = Fd::mul(q, n);
        n = Fd::sqr(n);
        n = Fd::select(degenerate, m, n);
        t = Fd::sqr(rr_alt);
        gej<V> r;
        r.z = Fd::mul(a.z, m_alt);
        t = Fd::add(t, q);
        r.x = t;
        t = Fd::template mul_int<2>(t);
        t = Fd::add(t, q);
        t = Fd::mul(t, rr_alt);
        t = Fd::add(t, n);
        r.y = Fd::neg(t);
        r.y = Fd::half(r.y);
        // a infinity -> (b.x, b.y, 1)
        r.x = Fd::select(a.inf, b.x, r.x);
        r.y = Fd::select(a.inf, b.y, r.y);
        r.z = Fd::select(a.inf, Fd::one(), r.z);
        r.inf = Fd::is_zero(r.z);
        return r;
    }

    // Jacobian -> affine for a non-infinity point (one inversion per lane).
    static ge<V> to_affine(gej<V> const& a) {
        F const zi = Fd::inverse(a.z);
        F const zi2 = Fd::sqr(zi);
        F const zi3 = Fd::mul(zi2, zi);
        ge<V> r; r.x = Fd::mul(a.x, zi2); r.y = Fd::mul(a.y, zi3); return r;
    }

    // Converts n Jacobian points (none infinity) to affine with a single
    // inversion per lane (Montgomery's trick).
    static void batch_to_affine(gej<V> const* in, ge<V>* out, std::size_t n, F* scratch) {
        if (n == 0) return;
        scratch[0] = in[0].z;
        for (std::size_t i = 1; i < n; ++i) scratch[i] = Fd::mul(scratch[i - 1], in[i].z);
        F inv = Fd::inverse(scratch[n - 1]);
        for (std::size_t i = n; i-- > 0;) {
            F zi = (i == 0) ? inv : Fd::mul(inv, scratch[i - 1]);
            if (i != 0) inv = Fd::mul(inv, in[i].z);
            F const zi2 = Fd::sqr(zi);
            out[i].x = Fd::mul(in[i].x, zi2);
            out[i].y = Fd::mul(in[i].y, Fd::mul(zi2, zi));
        }
    }
};

} // namespace secp256k1_turbo

#endif
