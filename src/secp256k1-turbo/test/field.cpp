// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {
using Fd = field<vec1>;

void check_eq(fe<vec1> const& got, mpz const& want, char const* what) {
    mpz const g = fe1_to_mpz(got);
    INFO(what);
    CHECK(mpz_cmp(g.v, want.v) == 0);
}
}

TEST_CASE("fe52 arithmetic matches GMP (portable backend)", "[field]") {
    rng r(0x5eed0001);
    for (int iter = 0; iter < 2000; ++iter) {
        // Mix canonical and loose (non-canonical, limbs < 2^52) inputs.
        fe<vec1> const a = (iter & 1) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
        fe<vec1> const b = (iter & 2) ? r.fe_loose() : mpz_to_fe1(r.below(P()));
        mpz const A = fe1_to_mpz(a), B = fe1_to_mpz(b);
        mpz t;
        INFO("iter " << iter << " a4=" << std::hex << a.n[4].v << " b4=" << b.n[4].v);

        mpz_mul(t.v, A.v, B.v); mpz_mod(t.v, t.v, P().v); check_eq(Fd::mul(a, b), t, "mul");
        mpz_mul(t.v, A.v, A.v); mpz_mod(t.v, t.v, P().v); check_eq(Fd::sqr(a), t, "sqr");
        mpz_add(t.v, A.v, B.v); mpz_mod(t.v, t.v, P().v); check_eq(Fd::add(a, b), t, "add");
        mpz_sub(t.v, A.v, B.v); mpz_mod(t.v, t.v, P().v); check_eq(Fd::sub(a, b), t, "sub");
        mpz_neg(t.v, A.v); mpz_mod(t.v, t.v, P().v); check_eq(Fd::neg(a), t, "neg");
        mpz_mul_ui(t.v, A.v, 2); mpz_mod(t.v, t.v, P().v); check_eq(Fd::mul_int<2>(a), t, "mul2");
        mpz_mul_ui(t.v, A.v, 3); mpz_mod(t.v, t.v, P().v); check_eq(Fd::mul_int<3>(a), t, "mul3");
        mpz_mul_ui(t.v, A.v, 8); mpz_mod(t.v, t.v, P().v); check_eq(Fd::mul_int<8>(a), t, "mul8");
        // half: 2 * half(a) == a
        check_eq(Fd::add(Fd::half(a), Fd::half(a)), A, "half");

        // normalize is canonical: limbs exactly equal the mpz representation.
        fe<vec1> n = a; Fd::normalize(n);
        fe<vec1> const canon = mpz_to_fe1(A);
        for (int i = 0; i < 5; ++i) CHECK(n.n[i].v == canon.n[i].v);
        CHECK((Fd::is_zero(a).v != 0) == (mpz_sgn(A.v) == 0));
        CHECK((Fd::is_odd(a).v != 0) == (mpz_odd_p(A.v) != 0));
        CHECK(Fd::eq(a, b).v == (mpz_cmp(A.v, B.v) == 0 ? ~std::uint64_t(0) : 0));
    }
}

TEST_CASE("fe52 inverse, sqrt and Euler criterion", "[field]") {
    rng r(0x5eed0002);
    for (int iter = 0; iter < 200; ++iter) {
        mpz const A = r.below(P());
        if (mpz_sgn(A.v) == 0) continue;
        fe<vec1> const a = mpz_to_fe1(A);
        mpz inv; mpz_invert(inv.v, A.v, P().v);
        check_eq(Fd::inverse(a), inv, "inverse");

        bool const square = mpz_legendre(A.v, P().v) == 1;
        CHECK((Fd::is_square_nonzero(a).v != 0) == square);
        fe<vec1> const s = Fd::sqrt_candidate(a);
        CHECK((Fd::eq(Fd::sqr(s), a).v != 0) == square);
        if (square) {
            // The candidate root is itself a square (even exponent).
            CHECK(Fd::is_square_nonzero(s).v != 0);
        }
    }
    CHECK(Fd::is_square_nonzero(Fd::zero()).v == 0);
}

TEST_CASE("fe52 edge values", "[field]") {
    // p - 1, p, p + 1 (as loose limbs), 2^256 - 1
    mpz pm1 = P(); mpz_sub_ui(pm1.v, pm1.v, 1);
    fe<vec1> const a = mpz_to_fe1(pm1);
    mpz t; mpz_mul(t.v, pm1.v, pm1.v); mpz_mod(t.v, t.v, P().v);
    check_eq(Fd::mul(a, a), t, "(p-1)^2");
    check_eq(Fd::sqr(a), t, "(p-1)^2 sqr");

    fe<vec1> p_limbs; p_limbs.n[0].v = fe_const::P0; p_limbs.n[1].v = p_limbs.n[2].v = p_limbs.n[3].v = fe_const::P1; p_limbs.n[4].v = fe_const::P4;
    CHECK(Fd::is_zero(p_limbs).v != 0);
    fe<vec1> all_ones; for (auto& l : all_ones.n) l.v = fe_const::M52;
    mpz big = from_limbs52(reinterpret_cast<std::uint64_t const*>(&all_ones));
    mpz_mod(big.v, big.v, P().v);
    check_eq(Fd::add(all_ones, Fd::zero()), big, "2^260-1 reduced");
    check_eq(Fd::mul(all_ones, all_ones), [&] { mpz q; mpz_mul(q.v, big.v, big.v); mpz_mod(q.v, q.v, P().v); return q; }(), "(2^260-1)^2");
}
