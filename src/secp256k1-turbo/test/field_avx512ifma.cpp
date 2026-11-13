// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compiled with the AVX-512 flags; every test skips unless the CPU supports IFMA.

#include <catch2/catch_test_macros.hpp>

#include "cpu.hpp"
#include "helpers.hpp"
#include "vec_avx512ifma.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {
using F8 = field<vec8>;
using F1 = field<vec1>;

fe<vec8> pack(fe<vec1> const (&a)[8]) {
    std::uint64_t limbs[5][8];
    for (int l = 0; l < 8; ++l) for (int i = 0; i < 5; ++i) limbs[i][l] = a[l].n[i].v;
    return F8::load(&limbs[0][0], 8);
}
fe<vec1> unpack(fe<vec8> const& a, int lane) {
    fe<vec1> r;
    for (int i = 0; i < 5; ++i) r.n[i].v = a.n[i].lane(lane);
    return r;
}
}

TEST_CASE("fe52 AVX-512 IFMA lanes match the portable backend", "[field][avx512]") {
    if (!cpu_supports_avx512ifma()) SKIP("CPU has no AVX-512 IFMA");
    rng r(0x5eed0003);
    for (int iter = 0; iter < 500; ++iter) {
        fe<vec1> a[8], b[8];
        for (int l = 0; l < 8; ++l) {
            a[l] = (l & 1) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
            b[l] = (l & 2) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
        }
        fe<vec8> const A = pack(a), B = pack(b);
        fe<vec8> const mul = F8::mul(A, B), sqr = F8::sqr(A), add = F8::add(A, B), sub = F8::sub(A, B), half = F8::half(A);
        fe<vec8> nrm = A; F8::normalize(nrm);
        vec8 const eqm = F8::eq(A, B), odd = F8::is_odd(A);
        for (int l = 0; l < 8; ++l) {
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

TEST_CASE("fe52 AVX-512 IFMA inverse and sqrt per lane", "[field][avx512]") {
    if (!cpu_supports_avx512ifma()) SKIP("CPU has no AVX-512 IFMA");
    rng r(0x5eed0004);
    for (int iter = 0; iter < 20; ++iter) {
        fe<vec1> a[8];
        for (auto& x : a) x = mpz_to_fe1(r.below(P()));
        fe<vec8> const A = pack(a);
        fe<vec8> const inv = F8::inverse(A), sq = F8::sqrt_candidate(A);
        vec8 const is_sq = F8::is_square_nonzero(A);
        for (int l = 0; l < 8; ++l) {
            for (int i = 0; i < 5; ++i) {
                CHECK(unpack(inv, l).n[i].v == F1::inverse(a[l]).n[i].v);
                CHECK(unpack(sq, l).n[i].v == F1::sqrt_candidate(a[l]).n[i].v);
            }
            CHECK(is_sq.lane(l) == F1::is_square_nonzero(a[l]).v);
        }
    }
}
