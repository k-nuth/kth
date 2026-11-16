// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {
bool same(u256 const& a, mpz const& b) { return mpz_cmp(from_u256(a).v, b.v) == 0; }
}

TEST_CASE("scalar mod n arithmetic matches GMP", "[scalar]") {
    rng r(0x5eed0010);
    mpz const& n = N();
    // Constants derived at compile time.
    {
        mpz r2; mpz_set_ui(r2.v, 1); mpz_mul_2exp(r2.v, r2.v, 512); mpz_mod(r2.v, r2.v, n.v);
        CHECK(same(sn::R2, r2));
        CHECK(static_cast<std::uint64_t>(sn::N.d[0] * (0 - sn::N_INV_NEG)) == 1);
        mpz nh; mpz_sub_ui(nh.v, n.v, 1); mpz_fdiv_q_2exp(nh.v, nh.v, 1);
        CHECK(same(sn::N_HALF, nh));
    }
    for (int iter = 0; iter < 2000; ++iter) {
        mpz const A = r.below(n), B = r.below(n);
        u256 const a = to_u256(A), b = to_u256(B);
        mpz t;
        mpz_mul(t.v, A.v, B.v); mpz_mod(t.v, t.v, n.v);
        CHECK(same(sn::mul_mod(a, b), t));
        CHECK(same(sn::from_mont(sn::mont_mul(sn::to_mont(a), sn::to_mont(b))), t));
        mpz_add(t.v, A.v, B.v); mpz_mod(t.v, t.v, n.v); CHECK(same(sn::add_mod(a, b), t));
        mpz_sub(t.v, A.v, B.v); mpz_mod(t.v, t.v, n.v); CHECK(same(sn::sub_mod(a, b), t));
        mpz_neg(t.v, A.v); mpz_mod(t.v, t.v, n.v); CHECK(same(sn::neg_mod(a), t));
        CHECK(sn::lt(a, b) == (mpz_cmp(A.v, B.v) < 0));
        // mul_shift_384 = round(A*B / 2^384)
        mpz q; mpz_mul(q.v, A.v, B.v); mpz_add_ui(q.v, q.v, 0);
        mpz half; mpz_set_ui(half.v, 1); mpz_mul_2exp(half.v, half.v, 383);
        mpz_add(q.v, q.v, half.v); mpz_fdiv_q_2exp(q.v, q.v, 384);
        CHECK(same(sn::mul_shift_384(a, b), q));
    }
}

TEST_CASE("scalar inverse and byte conversion", "[scalar]") {
    rng r(0x5eed0011);
    mpz const& n = N();
    for (int iter = 0; iter < 100; ++iter) {
        mpz const A = r.below(n);
        if (mpz_sgn(A.v) == 0) continue;
        u256 const a = to_u256(A);
        mpz inv; mpz_invert(inv.v, A.v, n.v);
        CHECK(same(sn::from_mont(sn::inverse_mont(sn::to_mont(a))), inv));
        bytes32 const be = to_be32(A);
        u256 back;
        CHECK(sn::from_be32(be.data(), back));
        CHECK(same(back, A));
        std::uint8_t out[32];
        sn::to_be32(back, out);
        CHECK(std::equal(out, out + 32, be.begin()));
    }
    // >= n reduces and reports overflow
    bytes32 nb = to_be32(n);
    u256 x;
    CHECK_FALSE(sn::from_be32(nb.data(), x));
    CHECK(sn::is_zero(x));
    bytes32 ff; ff.fill(0xFF);
    CHECK_FALSE(sn::from_be32(ff.data(), x));
    mpz t; mpz_set_ui(t.v, 1); mpz_mul_2exp(t.v, t.v, 256); mpz_sub_ui(t.v, t.v, 1); mpz_mod(t.v, t.v, n.v);
    CHECK(same(x, t));
}

TEST_CASE("GLV split: k == k1 + lambda * k2 with 128-bit halves", "[scalar]") {
    rng r(0x5eed0012);
    mpz const& n = N();
    mpz const lambda = from_u256(sn::LAMBDA);
    mpz two128; mpz_set_ui(two128.v, 1); mpz_mul_2exp(two128.v, two128.v, 128);
    auto check = [&](mpz const& K) {
        u256 const k = to_u256(K);
        sn::split_result const s = sn::split_lambda(k);
        mpz k1 = from_u256(s.k1), k2 = from_u256(s.k2);
        CHECK(mpz_cmp(k1.v, two128.v) < 0);
        CHECK(mpz_cmp(k2.v, two128.v) < 0);
        if (s.k1neg) mpz_neg(k1.v, k1.v);
        if (s.k2neg) mpz_neg(k2.v, k2.v);
        mpz t; mpz_mul(t.v, k2.v, lambda.v); mpz_add(t.v, t.v, k1.v); mpz_mod(t.v, t.v, n.v);
        CHECK(mpz_cmp(t.v, K.v) == 0);
    };
    for (int iter = 0; iter < 2000; ++iter) check(r.below(n));
    mpz e; mpz_set_ui(e.v, 0); check(e);
    mpz_set_ui(e.v, 1); check(e);
    mpz_sub_ui(e.v, n.v, 1); check(e);
    check(lambda);
    mpz l2; mpz_mul(l2.v, lambda.v, lambda.v); mpz_mod(l2.v, l2.v, n.v); check(l2);
}
