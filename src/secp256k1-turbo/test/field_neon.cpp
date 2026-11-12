// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The NEON lanes against the portable backend. Built on AArch64 only, where NEON
// is part of the baseline, so nothing here needs a run-time check.

#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"
#include "vec_neon.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {
using F2 = field<vec2>;
using F1 = field<vec1>;

fe<vec2> pack(fe<vec1> const (&a)[2]) {
    std::uint64_t limbs[5][2];
    for (int l = 0; l < 2; ++l) for (int i = 0; i < 5; ++i) limbs[i][l] = a[l].n[i].v;
    return F2::load(&limbs[0][0], 2);
}
fe<vec1> unpack(fe<vec2> const& a, int lane) {
    fe<vec1> r;
    for (int i = 0; i < 5; ++i) r.n[i].v = a.n[i].lane(lane);
    return r;
}
}

TEST_CASE("fe52 NEON lanes match the portable backend", "[field][neon]") {
    rng r(0x5eed0003);
    for (int iter = 0; iter < 500; ++iter) {
        fe<vec1> a[2], b[2];
        for (int l = 0; l < 2; ++l) {
            a[l] = (l & 1) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
            // Two lanes only, so the iteration varies which of them is loose.
            b[l] = ((iter + l) & 2) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
        }
        fe<vec2> const A = pack(a), B = pack(b);
        fe<vec2> const mul = F2::mul(A, B), sqr = F2::sqr(A), add = F2::add(A, B), sub = F2::sub(A, B), half = F2::half(A);
        fe<vec2> nrm = A; F2::normalize(nrm);
        vec2 const eqm = F2::eq(A, B), odd = F2::is_odd(A);
        for (int l = 0; l < 2; ++l) {
            for (int i = 0; i < 5; ++i) {
                CHECK(unpack(mul, l).n[i].v == F1::mul(a[l], b[l]).n[i].v);
                CHECK(unpack(sqr, l).n[i].v == F1::sqr(a[l]).n[i].v);
                CHECK(unpack(add, l).n[i].v == F1::add(a[l], b[l]).n[i].v);
                CHECK(unpack(sub, l).n[i].v == F1::sub(a[l], b[l]).n[i].v);
                CHECK(unpack(half, l).n[i].v == F1::half(a[l]).n[i].v);
                fe<vec1> n1 = a[l]; F1::normalize(n1);
                CHECK(unpack(nrm, l).n[i].v == n1.n[i].v);
            }
            CHECK(eqm.lane(l) == F1::eq(a[l], b[l]).v);
            CHECK(odd.lane(l) == F1::is_odd(a[l]).v);
        }
    }
}

TEST_CASE("fe52 NEON inverse and sqrt per lane", "[field][neon]") {
    rng r(0x5eed0004);
    for (int iter = 0; iter < 20; ++iter) {
        fe<vec1> a[2];
        for (auto& x : a) x = mpz_to_fe1(r.below(P()));
        fe<vec2> const A = pack(a);
        fe<vec2> const inv = F2::inverse(A), sq = F2::sqrt_candidate(A);
        vec2 const is_sq = F2::is_square_nonzero(A);
        for (int l = 0; l < 2; ++l) {
            for (int i = 0; i < 5; ++i) {
                CHECK(unpack(inv, l).n[i].v == F1::inverse(a[l]).n[i].v);
                CHECK(unpack(sq, l).n[i].v == F1::sqrt_candidate(a[l]).n[i].v);
            }
            CHECK(is_sq.lane(l) == F1::is_square_nonzero(a[l]).v);
        }
    }
}
