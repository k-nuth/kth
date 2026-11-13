// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The lane kernel against libsecp256k1's public API: u1*G + u2*Q including
// the degenerate cases (zero scalars, Q = +-G, sums landing on infinity).

#include <catch2/catch_test_macros.hpp>

#include <secp256k1.h>

#include "cpu.hpp"
#include "helpers.hpp"
#include "kernel.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {

secp256k1_context* ctx() {
    static secp256k1_context* c = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    return c;
}

bytes32 pubkey_x(secp256k1_pubkey const& p) {
    std::uint8_t ser[65]; std::size_t len = 65;
    secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &p, SECP256K1_EC_UNCOMPRESSED);
    bytes32 x; std::copy(ser + 1, ser + 33, x.begin()); return x;
}
bytes32 limbs_x(std::uint64_t const l[5]) {
    fe<vec1> f = field<vec1>::from_limbs(l); field<vec1>::normalize(f);
    std::uint64_t n[5]; for (int i = 0; i < 5; ++i) n[i] = f.n[i].v;
    bytes32 out; fe_limbs_to_be32(n, out.data()); return out;
}

struct expectation {
    bool infinity;
    bytes32 x;
    bool y_square;  // affine y is a quadratic residue
};

// u1*G + u2*Q via libsecp256k1.
expectation expected(u256 const& u1, u256 const& u2, secp256k1_pubkey const& q) {
    std::uint8_t b1[32], b2[32];
    sn::to_be32(u1, b1);
    sn::to_be32(u2, b2);
    secp256k1_pubkey pg, pq;
    bool const has_g = !sn::is_zero(u1) && secp256k1_ec_pubkey_create(ctx(), &pg, b1) == 1;
    secp256k1_pubkey tmp = q;
    bool const has_q = !sn::is_zero(u2) && secp256k1_ec_pubkey_tweak_mul(ctx(), &tmp, b2) == 1;
    if (has_q) pq = tmp;
    secp256k1_pubkey sum;
    expectation e{};
    if (has_g && has_q) {
        secp256k1_pubkey const* parts[2] = {&pg, &pq};
        if (secp256k1_ec_pubkey_combine(ctx(), &sum, parts, 2) != 1) { e.infinity = true; return e; }
    } else if (has_g) sum = pg;
    else if (has_q) sum = pq;
    else { e.infinity = true; return e; }
    std::uint8_t ser[65]; std::size_t len = 65;
    secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &sum, SECP256K1_EC_UNCOMPRESSED);
    std::copy(ser + 1, ser + 33, e.x.begin());
    mpz const y = from_be32(ser + 33);
    e.y_square = mpz_legendre(y.v, P().v) == 1;
    return e;
}

struct lane_case { u256 u1, u2; secp256k1_pubkey q; };

void fill(lane_batch& b, std::size_t lane, lane_case const& c, bool compressed, expectation const& e) {
    std::uint8_t ser[65]; std::size_t len = 65;
    secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &c.q, SECP256K1_EC_UNCOMPRESSED);
    std::uint64_t x[5], y[5];
    fe_limbs_from_be32(ser + 1, x);
    fe_limbs_from_be32(ser + 33, y);
    for (int i = 0; i < 5; ++i) { b.qx[i][lane] = x[i]; b.qy[i][lane] = compressed ? 0 : y[i]; b.rn[i][lane] = 0; }
    b.decompress[lane] = compressed ? ~std::uint64_t(0) : 0;
    b.parity_odd[lane] = (ser[64] & 1) ? ~std::uint64_t(0) : 0;
    b.use_rn[lane] = 0;
    lane_set_scalars(b, lane, c.u1, c.u2);
    std::uint64_t r[5];
    if (e.infinity) { for (auto& l : r) l = 0; r[0] = 1; }
    else fe_limbs_from_be32(e.x.data(), r);
    for (int i = 0; i < 5; ++i) b.r[i][lane] = r[i];
}

std::vector<kernel_fn> kernels() {
    std::vector<kernel_fn> k = {&kernel_scalar};
#if defined(KTH_SECP256K1_TURBO_HAVE_AVX512IFMA)
    if (cpu_supports_avx512ifma()) k.push_back(&kernel_avx512ifma);
#endif
    return k;
}

secp256k1_pubkey pubkey_of(u256 const& k) {
    std::uint8_t b[32]; sn::to_be32(k, b);
    secp256k1_pubkey p; REQUIRE(secp256k1_ec_pubkey_create(ctx(), &p, b) == 1);
    return p;
}
secp256k1_pubkey negate(secp256k1_pubkey p) { REQUIRE(secp256k1_ec_pubkey_negate(ctx(), &p) == 1); return p; }

void run_cases(std::vector<lane_case> const& cases) {
    REQUIRE(cases.size() <= max_lanes);
    for (kernel_fn kernel : kernels()) {
        for (bool compressed : {false, true}) {
            lane_batch b{};
            std::vector<expectation> ex;
            for (std::size_t i = 0; i < max_lanes; ++i) {
                lane_case const& c = cases[i % cases.size()];
                ex.push_back(expected(c.u1, c.u2, c.q));
                fill(b, i, c, compressed, ex.back());
            }
            kernel(b, check_mode::ecdsa);
            for (std::size_t i = 0; i < max_lanes; ++i) {
                INFO("lane " << i << " compressed=" << compressed << " oncurve=" << (b.dbg[0][i] != 0) << " inf=" << (b.dbg[1][i] != 0) << " xok=" << (b.dbg[2][i] != 0) << " k1neg=" << (b.k1neg[i] != 0) << " k2neg=" << (b.k2neg[i] != 0));
                CHECK((b.valid[i] != 0) == !ex[i].infinity);
                if (!ex[i].infinity) {
                    using F1 = field<vec1>;
                    std::uint64_t xl[5], zl[5], rl[5];
                    for (int k = 0; k < 5; ++k) { xl[k] = b.px[k][i]; zl[k] = b.pz[k][i]; rl[k] = b.r[k][i]; }
                    {
                        fe<vec1> const X = F1::from_limbs(xl), Z = F1::from_limbs(zl), R = F1::from_limbs(rl);
                        fe<vec1> const rz2 = F1::mul(R, F1::sqr(Z));
                        fe<vec1> d = F1::sub(X, rz2);
                        fe<vec1> dn = d; F1::normalize(dn);
                        fe<vec1> xn = X; F1::normalize(xn);
                        fe<vec1> rn = rz2; F1::normalize(rn);
                        INFO(std::hex << "X=" << xn.n[4].v << ":" << xn.n[3].v << ":" << xn.n[2].v << ":" << xn.n[1].v << ":" << xn.n[0].v
                             << " rZ2=" << rn.n[4].v << ":" << rn.n[3].v << ":" << rn.n[2].v << ":" << rn.n[1].v << ":" << rn.n[0].v
                             << " sub(raw)=" << d.n[4].v << ":" << d.n[3].v << ":" << d.n[2].v << ":" << d.n[1].v << ":" << d.n[0].v
                             << " sub(norm)=" << dn.n[4].v << ":" << dn.n[3].v << ":" << dn.n[2].v << ":" << dn.n[1].v << ":" << dn.n[0].v
                             << " Zraw=" << zl[4] << ":" << zl[3] << ":" << zl[2] << ":" << zl[1] << ":" << zl[0]);
                        CHECK(F1::eq(X, rz2).v != 0);
                    }
                    fe<vec1> const zi = F1::inverse(F1::from_limbs(zl));
                    fe<vec1> ax = F1::mul(F1::from_limbs(xl), F1::sqr(zi));
                    std::uint64_t al[5]; for (int k = 0; k < 5; ++k) al[k] = ax.n[k].v;
                    CHECK(limbs_x(al) == ex[i].x);
                }
            }
            kernel(b, check_mode::schnorr_bch);
            for (std::size_t i = 0; i < max_lanes; ++i) {
                INFO("lane " << i << " schnorr compressed=" << compressed << " oncurve=" << (b.dbg[0][i] != 0) << " inf=" << (b.dbg[1][i] != 0) << " xok=" << (b.dbg[2][i] != 0) << " ysq=" << (b.dbg[3][i] != 0) << " expected ysq=" << ex[i].y_square);
                CHECK((b.valid[i] != 0) == (!ex[i].infinity && ex[i].y_square));
            }
        }
    }
}

} // namespace

TEST_CASE("G comb table entries are c * G + c' * 2^128 * G", "[kernel]") {
    g_table const& t = get_g_table();
    for (int d = 1; d < g_table::entries; ++d) {
        int const c = d & 15, c2 = d >> 4;
        mpz s; mpz_set_ui(s.v, static_cast<unsigned long>(c2)); mpz_mul_2exp(s.v, s.v, 128); mpz_add_ui(s.v, s.v, static_cast<unsigned long>(c));
        secp256k1_pubkey const p = pubkey_of(to_u256(s));
        std::uint64_t x[5], y[5];
        for (int i = 0; i < 5; ++i) { x[i] = t.t[d][i]; y[i] = t.t[d][5 + i]; }
        std::uint8_t ser[65]; std::size_t len = 65;
        secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &p, SECP256K1_EC_UNCOMPRESSED);
        INFO("d=" << d);
        CHECK(limbs_x(x) == pubkey_x(p));
        bytes32 ey; std::copy(ser + 33, ser + 65, ey.begin());
        CHECK(limbs_x(y) == ey);
        // lookup_g returns the same entry
        ge<vec1> const got = ecmult<vec1>::lookup_g(t, vec1::set1(static_cast<std::uint64_t>(d)));
        std::uint64_t gx[5]; for (int i = 0; i < 5; ++i) gx[i] = got.x.n[i].v;
        CHECK(limbs_x(gx) == pubkey_x(p));
    }
}

TEST_CASE("Q table entries are d * Q (portable backend)", "[kernel]") {
    rng r(0x5eed0023);
    using E = ecmult<vec1>;
    for (int round = 0; round < 4; ++round) {
        mpz k = r.below(N()); if (mpz_sgn(k.v) == 0) mpz_set_ui(k.v, 3);
        secp256k1_pubkey const Q = pubkey_of(to_u256(k));
        std::uint8_t ser[65]; std::size_t len = 65;
        secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &Q, SECP256K1_EC_UNCOMPRESSED);
        ge<vec1> q;
        std::uint64_t x[5], y[5];
        fe_limbs_from_be32(ser + 1, x); fe_limbs_from_be32(ser + 33, y);
        q.x = field<vec1>::from_limbs(x); q.y = field<vec1>::from_limbs(y);
        E::q_table t;
        E::build_q_table(q, t);
        for (int d = 1; d <= 15; ++d) {
            mpz s; mpz_mul_ui(s.v, k.v, static_cast<unsigned long>(d)); mpz_mod(s.v, s.v, N().v);
            secp256k1_pubkey const p = pubkey_of(to_u256(s));
            std::uint64_t const* e = t.data + d * E::q_entry_u64;
            INFO("d=" << d);
            CHECK(limbs_x(e) == pubkey_x(p));
            // lookup_q must return the same entry
            ge<vec1> const got = E::lookup_q(t, vec1::set1(d), false, vec1::zero(), vec1::zero());
            std::uint64_t gx[5]; for (int i = 0; i < 5; ++i) gx[i] = got.x.n[i].v;
            CHECK(limbs_x(gx) == pubkey_x(p));
            // lambda entry: beta * x
            ge<vec1> const lam = E::lookup_q(t, vec1::set1(d), true, vec1::zero(), vec1::zero());
            fe<vec1> const bx = field<vec1>::mul(field<vec1>::beta(), got.x);
            CHECK(field<vec1>::eq(lam.x, bx).v != 0);
        }
    }
}

TEST_CASE("kernel: random double scalar multiplications", "[kernel]") {
    rng r(0x5eed0020);
    for (int round = 0; round < 8; ++round) {
        std::vector<lane_case> cases;
        for (int i = 0; i < 8; ++i) {
            lane_case c;
            c.u1 = to_u256(r.below(N()));
            c.u2 = to_u256(r.below(N()));
            mpz k = r.below(N()); if (mpz_sgn(k.v) == 0) mpz_set_ui(k.v, 1);
            c.q = pubkey_of(to_u256(k));
            cases.push_back(c);
        }
        run_cases(cases);
    }
}

TEST_CASE("kernel: degenerate scalars and points", "[kernel]") {
    rng r(0x5eed0021);
    u256 const one = sn::ONE;
    u256 nm1; sn::sub_raw(nm1, sn::N, one);
    secp256k1_pubkey const G = pubkey_of(one);
    mpz k = r.below(N()); if (mpz_sgn(k.v) == 0) mpz_set_ui(k.v, 2);
    u256 const kk = to_u256(k);
    secp256k1_pubkey const Q = pubkey_of(kk);
    u256 const a = to_u256(r.below(N()));
    u256 const kinv = sn::from_mont(sn::inverse_mont(sn::to_mont(kk)));

    std::vector<lane_case> cases = {
        {sn::ZERO, sn::ZERO, Q},           // infinity
        {a, sn::ZERO, Q},                  // only G part
        {sn::ZERO, a, Q},                  // only Q part
        {one, one, G},                     // G + G: doubling inside the add
        {a, a, negate(G)},                 // a*G - a*G = infinity
        {nm1, one, G},                     // (n-1)G + G = infinity
        {a, sn::neg_mod(sn::mul_mod(a, kinv)), Q},  // a*G - (a/k)*(kG) = infinity
        {nm1, nm1, Q},                     // largest scalars
    };
    run_cases(cases);
    // Scalars whose GLV halves are negative/positive in every combination.
    std::vector<lane_case> more;
    for (int i = 0; i < 8; ++i) more.push_back({to_u256(r.below(N())), to_u256(r.below(N())), (i & 1) ? Q : G});
    run_cases(more);
}
