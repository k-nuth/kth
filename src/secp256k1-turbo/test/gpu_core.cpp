// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The GPU kernel compiled for the host: its 8x32 field against GMP, its
// group law against the 5x52 lane implementation, and whole verifications
// against the portable backend. This runs everywhere, GPU or not, so the
// device code is tested on every CI target.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include "kernel_core.h"

#include "g_table.hpp"
#include "gpu_backend.hpp"
#include "group.hpp"
#include "helpers.hpp"
#include "prepare.hpp"
#include "sha256.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {

// 8 little-endian words <-> integers, and the device field type through them.
mpz words_to_mpz(u32 const w[8]) { mpz r; mpz_import(r.v, 8, -1, 4, 0, 0, w); return r; }
void mpz_to_words(mpz const& a, u32 w[8]) {
    for (int i = 0; i < 8; ++i) w[i] = 0;
    std::size_t count = 0;
    mpz_export(w, &count, -1, 4, 0, 0, a.v);
}
fe8 mpz_to_fe(mpz const& a) { u32 w[8]; mpz_to_words(a, w); return fe_from_words(w); }
// Value mod p of any representative.
mpz value(fe8 const& a) { u32 w[8]; fe_to_words(a, w); return words_to_mpz(w); }
fe8 fe1_to_fe(fe<vec1> const& a) { return mpz_to_fe(fe1_to_mpz(a)); }

mpz mod_p(mpz a) { mpz_mod(a.v, a.v, P().v); return a; }

// Representation invariant every operation must maintain (any value below 2^256).
bool well_formed(fe8 const&) { return true; }

struct edge_values {
    std::vector<fe8> v;
    edge_values() {
        mpz t;
        mpz_set_ui(t.v, 0); v.push_back(mpz_to_fe(t));
        mpz_set_ui(t.v, 1); v.push_back(mpz_to_fe(t));
        mpz_set_ui(t.v, 2); v.push_back(mpz_to_fe(t));
        mpz_sub_ui(t.v, P().v, 1); v.push_back(mpz_to_fe(t));
        mpz_sub_ui(t.v, P().v, 2); v.push_back(mpz_to_fe(t));
        mpz_set_ui(t.v, 1); mpz_mul_2exp(t.v, t.v, 255); v.push_back(mpz_to_fe(t));
        mpz_set_ui(t.v, 1); mpz_mul_2exp(t.v, t.v, 32); mpz_add_ui(t.v, t.v, 977); v.push_back(mpz_to_fe(t));  // 2^32 + 977
        mpz_set_ui(t.v, 1); mpz_mul_2exp(t.v, t.v, 224); mpz_sub_ui(t.v, t.v, 1); v.push_back(mpz_to_fe(t));
        mpz_set_str(t.v, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2E", 16); v.push_back(mpz_to_fe(t));
        // non-canonical representatives: p, p + 1, 2^256 - 1
        v.push_back(mpz_to_fe(P()));
        mpz_add_ui(t.v, P().v, 1); v.push_back(mpz_to_fe(t));
        mpz_set_ui(t.v, 1); mpz_mul_2exp(t.v, t.v, 256); mpz_sub_ui(t.v, t.v, 1); v.push_back(mpz_to_fe(t));
    }
};

secp256k1_context* ctx() {
    static secp256k1_context* c = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    return c;
}

// A random affine point as the 5x52 lane type (random key * G via libsecp256k1).
ge<vec1> random_point(rng& r) {
    bytes32 seckey;
    do { seckey = r.b32(); } while (secp256k1_ec_seckey_verify(ctx(), seckey.data()) != 1);
    secp256k1_pubkey pk;
    REQUIRE(secp256k1_ec_pubkey_create(ctx(), &pk, seckey.data()) == 1);
    std::uint8_t ser[65]; std::size_t len = 65;
    secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &pk, SECP256K1_EC_UNCOMPRESSED);
    std::uint64_t lx[5], ly[5];
    fe_limbs_from_be32(ser + 1, lx);
    fe_limbs_from_be32(ser + 33, ly);
    ge<vec1> p;
    p.x = field<vec1>::from_limbs(lx);
    p.y = field<vec1>::from_limbs(ly);
    return p;
}
ge8 to_ge26(ge<vec1> const& p) { ge8 r; r.x = fe1_to_fe(p.x); r.y = fe1_to_fe(p.y); return r; }

// Affine x, y of a Jacobian point, as integers, for both representations.
struct affine { mpz x, y; bool inf; };
affine affine_of(gej<vec1> const& a) {
    affine r; r.inf = a.inf.v != 0;
    if (r.inf) return r;
    ge<vec1> const g = group<vec1>::to_affine(a);
    r.x = fe1_to_mpz(g.x); r.y = fe1_to_mpz(g.y);
    return r;
}
affine affine_of(gej8 const& a) {
    affine r; r.inf = a.inf != 0;
    if (r.inf) return r;
    fe8 const zi = fe_inverse(a.z);
    fe8 const zi2 = fe_sqr(zi);
    r.x = value(fe_mul(a.x, zi2));
    r.y = value(fe_mul(a.y, fe_mul(zi2, zi)));
    return r;
}
void check_same(affine const& a, affine const& b) {
    REQUIRE(a.inf == b.inf);
    if (a.inf) return;
    CHECK(mpz_cmp(a.x.v, b.x.v) == 0);
    CHECK(mpz_cmp(a.y.v, b.y.v) == 0);
}

} // namespace

TEST_CASE("gpu field: add, sub, neg, half, small multiples against GMP", "[gpu][field]") {
    rng r(0x6e0001);
    edge_values const edges;
    std::vector<fe8> vals = edges.v;
    for (int i = 0; i < 2000; ++i) vals.push_back(mpz_to_fe(r.below(P())));
    for (std::size_t i = 0; i < vals.size(); ++i) {
        fe8 const a = vals[i];
        fe8 const b = vals[(i * 7 + 3) % vals.size()];
        mpz const ma = value(a), mb = value(b);
        mpz t;
        mpz_add(t.v, ma.v, mb.v); t = mod_p(t);
        fe8 const s = fe_add(a, b);
        CHECK(well_formed(s)); CHECK(mpz_cmp(value(s).v, t.v) == 0);
        mpz_sub(t.v, ma.v, mb.v); t = mod_p(t);
        fe8 const d = fe_sub(a, b);
        CHECK(well_formed(d)); CHECK(mpz_cmp(value(d).v, t.v) == 0);
        mpz_neg(t.v, ma.v); t = mod_p(t);
        fe8 const n = fe_neg(a);
        CHECK(well_formed(n)); CHECK(mpz_cmp(value(n).v, t.v) == 0);
        // half: 2 * half(a) == a
        fe8 const h = fe_half(a);
        CHECK(well_formed(h));
        CHECK(mpz_cmp(value(fe_add(h, h)).v, ma.v) == 0);
        for (u32 k = 2; k <= 8; ++k) {
            mpz_mul_ui(t.v, ma.v, k); t = mod_p(t);
            fe8 const m = fe_mul_small(a, k);
            CHECK(well_formed(m)); CHECK(mpz_cmp(value(m).v, t.v) == 0);
        }
        CHECK(well_formed(fe_normalize(a)));
        CHECK((fe_is_zero(a) != 0) == (mpz_sgn(ma.v) == 0));
        CHECK((fe_is_odd(a) != 0) == (mpz_odd_p(ma.v) != 0));
        CHECK(fe_eq(a, a) == 0xFFFFFFFFu);
        CHECK((fe_eq(a, b) != 0) == (mpz_cmp(ma.v, mb.v) == 0));
    }
}

TEST_CASE("gpu field: mul and sqr against GMP", "[gpu][field]") {
    rng r(0x6e0002);
    edge_values const edges;
    std::vector<fe8> vals = edges.v;
    for (int i = 0; i < 3000; ++i) vals.push_back(mpz_to_fe(r.below(P())));
    for (std::size_t i = 0; i < vals.size(); ++i) {
        fe8 const a = vals[i];
        fe8 const b = vals[(i * 11 + 5) % vals.size()];
        mpz const ma = value(a), mb = value(b);
        mpz t;
        mpz_mul(t.v, ma.v, mb.v); t = mod_p(t);
        fe8 const m = fe_mul(a, b);
        CHECK(well_formed(m)); CHECK(mpz_cmp(value(m).v, t.v) == 0);
        mpz_mul(t.v, ma.v, ma.v); t = mod_p(t);
        fe8 const s = fe_sqr(a);
        CHECK(well_formed(s)); CHECK(mpz_cmp(value(s).v, t.v) == 0);
        // products of loosely normalized operands (sums, differences)
        fe8 const m2 = fe_mul(fe_add(a, b), fe_sub(a, b));
        mpz u; mpz_add(u.v, ma.v, mb.v); mpz_sub(t.v, ma.v, mb.v); mpz_mul(t.v, t.v, u.v); t = mod_p(t);
        CHECK(well_formed(m2)); CHECK(mpz_cmp(value(m2).v, t.v) == 0);
    }
    // all pairs of the edge values
    for (fe8 const& a : edges.v) for (fe8 const& b : edges.v) {
        mpz t; mpz_mul(t.v, value(a).v, value(b).v); t = mod_p(t);
        CHECK(mpz_cmp(value(fe_mul(a, b)).v, t.v) == 0);
    }
}

TEST_CASE("gpu field: inverse, sqrt and Euler criterion", "[gpu][field]") {
    rng r(0x6e0003);
    for (int i = 0; i < 64; ++i) {
        mpz const ma = r.below(P());
        if (mpz_sgn(ma.v) == 0) continue;
        fe8 const a = mpz_to_fe(ma);
        fe8 const inv = fe_inverse(a);
        CHECK(fe_eq(fe_mul(a, inv), fe_one()) == 0xFFFFFFFFu);
        mpz t; mpz_powm_ui(t.v, ma.v, 2, P().v);
        fe8 const sq = mpz_to_fe(t);
        fe8 const root = fe_sqrt_candidate(sq);
        CHECK(fe_eq(fe_sqr(root), sq) == 0xFFFFFFFFu);
        CHECK(fe_is_square_nonzero(sq) == 0xFFFFFFFFu);
        CHECK(fe_is_square_nonzero(fe_neg(sq)) == 0);  // -1 is a non-residue mod p (p = 3 mod 4)
    }
    CHECK(fe_is_square_nonzero(fe_zero()) == 0);
    // The variable-time inverse against GMP: random values, and the
    // structured ones that stress the divstep bounds (small, near p, few
    // set bits, long runs of ones).
    {
        std::vector<mpz> vals;
        for (int i = 0; i < 20000; ++i) vals.push_back(r.below(P()));
        for (int k = 0; k < 256; ++k) {
            mpz t; mpz_ui_pow_ui(t.v, 2, k); vals.push_back(t);
            mpz u; mpz_sub_ui(u.v, t.v, 1); if (mpz_sgn(u.v) > 0) vals.push_back(u);
            mpz w; mpz_add_ui(w.v, t.v, 1); vals.push_back(w);
            mpz x; mpz_sub(x.v, P().v, t.v); vals.push_back(x);
        }
        for (int i = 1; i < 300; ++i) { mpz t; mpz_set_ui(t.v, i); vals.push_back(t); mpz u; mpz_sub_ui(u.v, P().v, i); vals.push_back(u); }
        for (std::size_t i = 0; i < vals.size(); ++i) {
            mpz v; mpz_mod(v.v, vals[i].v, P().v);
            if (mpz_sgn(v.v) == 0) continue;
            mpz want; mpz_invert(want.v, v.v, P().v);
            INFO("inverse_var value " << i);
            CHECK(mpz_cmp(value(fe_inverse_var(mpz_to_fe(v))).v, want.v) == 0);
        }
    }
    fe8 const beta = fe_beta();
    CHECK(fe_eq(fe_mul(fe_sqr(beta), beta), fe_one()) == 0xFFFFFFFFu);  // cube root of unity
    CHECK(fe_eq(beta, fe_one()) == 0);
}

TEST_CASE("gpu group law agrees with the lane implementation", "[gpu][group]") {
    using Gr = group<vec1>;
    rng r(0x6e0004);
    for (int i = 0; i < 100; ++i) {
        ge<vec1> const a = random_point(r);
        ge<vec1> const b = random_point(r);
        gej<vec1> const aj = Gr::from_ge(a);
        gej8 const aj8 = gej_from_ge(to_ge26(a));
        check_same(affine_of(Gr::dbl(aj)), affine_of(gej_dbl(aj8)));
        check_same(affine_of(Gr::add_ge(aj, b)), affine_of(gej_add_ge(aj8, to_ge26(b))));
        // degenerate cases: a + a, a + (-a), inf + b, (a + b) + c with projective a
        check_same(affine_of(Gr::add_ge(aj, a)), affine_of(gej_add_ge(aj8, to_ge26(a))));
        check_same(affine_of(Gr::add_ge(aj, Gr::neg(a))), affine_of(gej_add_ge(aj8, ge_cneg(0xFFFFFFFFu, to_ge26(a)))));
        check_same(affine_of(Gr::add_ge(Gr::infinity(), b)), affine_of(gej_add_ge(gej_infinity(), to_ge26(b))));
        gej<vec1> const ab = Gr::add_ge(Gr::dbl(aj), b);
        gej8 const ab8 = gej_add_ge(gej_dbl(aj8), to_ge26(b));
        check_same(affine_of(ab), affine_of(ab8));
        check_same(affine_of(Gr::add_ge(ab, Gr::neg(b))), affine_of(gej_add_ge(ab8, ge_cneg(0xFFFFFFFFu, to_ge26(b)))));
        // infinity reached from projective coordinates: (2a) + (-2a)
        gej<vec1> const twoa = Gr::dbl(aj);
        ge<vec1> const twoa_aff = Gr::to_affine(twoa);
        gej8 const twoa8 = gej_dbl(aj8);
        check_same(affine_of(Gr::add_ge(twoa, Gr::neg(twoa_aff))), affine_of(gej_add_ge(twoa8, ge_cneg(0xFFFFFFFFu, to_ge26(twoa_aff)))));
        CHECK(gej_add_ge(twoa8, ge_cneg(0xFFFFFFFFu, to_ge26(twoa_aff))).inf == 0xFFFFFFFFu);
    }
}

TEST_CASE("gpu G table matches the lane G table", "[gpu][table]") {
    g_table const& g = get_g_table();
    std::uint32_t const* w = gpu::g_table_words();
    for (int d = 1; d < GPU_G_ENTRIES; ++d) {
        ge8 const p = gtab_lookup(w, static_cast<u32>(d));
        fe<vec1> const x = field<vec1>::from_limbs(&g.t[d][0]);
        fe<vec1> const y = field<vec1>::from_limbs(&g.t[d][5]);
        CHECK(mpz_cmp(value(p.x).v, fe1_to_mpz(x).v) == 0);
        CHECK(mpz_cmp(value(p.y).v, fe1_to_mpz(y).v) == 0);
    }
}

namespace {

struct item {
    std::vector<std::uint8_t> sig = std::vector<std::uint8_t>(64);
    bytes32 msg{};
    std::vector<std::uint8_t> pubkey;
    signature_input view() const { return {sig.data(), msg.data(), pubkey.data(), pubkey.size()}; }
};

item make_item(rng& r, bool schnorr, int form) {
    item it;
    bytes32 seckey;
    do { seckey = r.b32(); } while (secp256k1_ec_seckey_verify(ctx(), seckey.data()) != 1);
    it.msg = r.b32();
    secp256k1_pubkey pk;
    REQUIRE(secp256k1_ec_pubkey_create(ctx(), &pk, seckey.data()) == 1);
    std::uint8_t ser[65]; std::size_t len = 65;
    if (form == 0) { len = 33; secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &pk, SECP256K1_EC_COMPRESSED); }
    else { secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &pk, SECP256K1_EC_UNCOMPRESSED); if (form == 2) ser[0] = 0x06 | (ser[64] & 1); }
    it.pubkey.assign(ser, ser + len);
    if (schnorr) {
        REQUIRE(secp256k1_schnorr_sign(ctx(), it.sig.data(), it.msg.data(), seckey.data(), nullptr, nullptr) == 1);
    } else {
        secp256k1_ecdsa_signature sig;
        REQUIRE(secp256k1_ecdsa_sign(ctx(), &sig, it.msg.data(), seckey.data(), nullptr, nullptr) == 1);
        secp256k1_ecdsa_signature_serialize_compact(ctx(), it.sig.data(), &sig);
    }
    return it;
}

} // namespace

TEST_CASE("gpu kernel on the host gives the portable backend's verdicts", "[gpu][verify]") {
    rng r(0x6e0005);
    for (bool schnorr : {false, true}) {
        std::vector<item> items;
        std::vector<signature_input> in;
        for (int i = 0; i < 48; ++i) {
            item it = make_item(r, schnorr, i % 3);
            switch (i % 6) {
                case 1: it.sig[r.u64() % 64] ^= static_cast<std::uint8_t>(1 << (r.u64() % 8)); break;  // corrupt signature
                case 2: it.msg[0] ^= 1; break;                                                          // wrong message
                case 3: it.pubkey = make_item(r, schnorr, 0).pubkey; break;                             // wrong key
                case 4: it.pubkey[1 + (r.u64() % 32)] ^= 0x10; break;                                   // off curve / other key
                default: break;
            }
            items.push_back(it);
        }
        for (auto const& it : items) in.push_back(it.view());
        std::vector<std::uint8_t> expected(items.size());
        std::size_t const want = schnorr ? verify_schnorr_bch(in, expected, backend::scalar_portable) : verify_ecdsa(in, expected, backend::scalar_portable);
        std::size_t got = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            gpu_raw_input gi;
            gpu::fill_raw_input(in[i], gi);
            std::uint32_t const v = gpu::verify_one_host(gi, schnorr);
            INFO((schnorr ? "schnorr" : "ecdsa") << " item " << i);
            CHECK((v != 0) == (expected[i] != 0));
            got += v != 0;
        }
        CHECK(got == want);
        CHECK(want >= 16);  // the untouched third verifies
    }
}

TEST_CASE("gpu Schnorr challenge matches SHA-256 of r, compressed key and message", "[gpu][sha256]") {
    rng r(0x6e0008);
    // Random fields, plus all-zero and all-one bytes, which show any word
    // shifted across the one-byte offset of the key prefix.
    std::vector<std::array<bytes32, 3>> cases;
    for (int i = 0; i < 4000; ++i) cases.push_back({r.b32(), r.b32(), r.b32()});
    bytes32 zero{}, ones{};
    ones.fill(0xFF);
    for (int mask = 0; mask < 8; ++mask) cases.push_back({(mask & 1) ? ones : zero, (mask & 2) ? ones : zero, (mask & 4) ? ones : zero});
    for (std::size_t i = 0; i < cases.size(); ++i) {
        auto const& [rb, xb, mb] = cases[i];
        for (int form = 0; form < 3; ++form) {   // compressed, uncompressed, hybrid
            bool const odd = ((i + form) & 1) != 0;
            std::uint8_t pk[65];
            std::size_t pk_size = 33;
            pk[0] = odd ? 0x03 : 0x02;
            std::memcpy(pk + 1, xb.data(), 32);
            if (form != 0) {
                pk_size = 65;
                pk[0] = form == 1 ? 0x04 : (odd ? 0x07 : 0x06);
                bytes32 y = r.b32();
                y[31] = static_cast<std::uint8_t>((y[31] & 0xFE) | (odd ? 1 : 0));
                std::memcpy(pk + 33, y.data(), 32);
            }
            std::uint8_t sig[64];
            std::memcpy(sig, rb.data(), 32);
            std::memset(sig + 32, 0, 32);
            signature_input const in{sig, mb.data(), pk, pk_size};
            gpu_raw_input gi;
            gpu::fill_raw_input(in, gi);
            REQUIRE((gi.flags & GPU_FLAG_OK) != 0);
            sc8 const e = gpu_schnorr_challenge(&gi, gi.flags & GPU_FLAG_PARITY_ODD);
            std::uint8_t buf[97];
            std::memcpy(buf, rb.data(), 32);
            buf[32] = odd ? 0x03 : 0x02;
            std::memcpy(buf + 33, xb.data(), 32);
            std::memcpy(buf + 65, mb.data(), 32);
            std::uint8_t h[32];
            sha256(buf, sizeof(buf), h);
            u32 want[8];
            for (int k = 0; k < 8; ++k) {
                std::uint8_t const* q = h + 28 - 4 * k;
                want[k] = (u32(q[0]) << 24) | (u32(q[1]) << 16) | (u32(q[2]) << 8) | u32(q[3]);
            }
            INFO("case " << i << " form " << form);
            CHECK(std::memcmp(e.n, want, sizeof(want)) == 0);
        }
    }
}

namespace {

sc8 sc_of(u256 const& a) {
    sc8 r;
    for (int i = 0; i < 4; ++i) { r.n[2 * i] = static_cast<u32>(a.d[i]); r.n[2 * i + 1] = static_cast<u32>(a.d[i] >> 32); }
    return r;
}
bool sc_same(sc8 const& a, u256 const& b) {
    sc8 const c = sc_of(b);
    for (int i = 0; i < 8; ++i) if (a.n[i] != c.n[i]) return false;
    return true;
}

} // namespace

TEST_CASE("gpu scalar arithmetic matches scalar256", "[gpu][scalar]") {
    rng r(0x6e0007);
    std::vector<u256> vals = {sn::ZERO, sn::ONE, sn::N_HALF};
    { u256 t = sn::N; t.d[0] -= 1; vals.push_back(t); }   // n - 1
    for (int i = 0; i < 20000; ++i) vals.push_back(to_u256(r.below(N())));
    for (int k = 0; k < 256; ++k) {
        mpz t; mpz_ui_pow_ui(t.v, 2, k); vals.push_back(to_u256(t));
        mpz u; mpz_add_ui(u.v, t.v, 1); if (mpz_cmp(u.v, N().v) < 0) vals.push_back(to_u256(u));
        mpz w; mpz_sub(w.v, N().v, t.v); vals.push_back(to_u256(w));
    }
    for (std::size_t i = 0; i < vals.size(); ++i) {
        u256 const a = vals[i];
        u256 const b = vals[(i * 7 + 3) % vals.size()];
        INFO("item " << i);
        CHECK(sc_same(sc_mul_mod(sc_of(a), sc_of(b)), sn::mul_mod(a, b)));
        CHECK(sc_same(sc_add_mod(sc_of(a), sc_of(b)), sn::add_mod(a, b)));
        CHECK(sc_same(sc_neg_mod(sc_of(a)), sn::neg_mod(a)));
        if (!sn::is_zero(a)) CHECK(sc_same(sc_inverse_var(sc_of(a)), sn::from_mont(sn::inverse_mont(sn::to_mont(a)))));
        sn::split_result const want = sn::split_lambda(a);
        sc_split const got = sc_split_lambda(sc_of(a));
        CHECK((got.k1neg != 0) == want.k1neg);
        CHECK((got.k2neg != 0) == want.k2neg);
        sc8 const k1 = sc_of(want.k1), k2 = sc_of(want.k2);
        for (int j = 0; j < 4; ++j) { CHECK(got.k1[j] == k1.n[j]); CHECK(got.k2[j] == k2.n[j]); }
    }
    // values at and above n reduce like from_be32 (one subtraction)
    for (int i = 0; i < 200; ++i) {
        mpz big = r.below(N()); mpz_add(big.v, big.v, N().v);
        mpz two256; mpz_ui_pow_ui(two256.v, 2, 256);
        if (mpz_cmp(big.v, two256.v) >= 0) continue;
        u32 w[8]; mpz_to_words(big, w);
        sc8 got = sc_reduce(sc_from_words(w));
        mpz want; mpz_mod(want.v, big.v, N().v);
        CHECK(sc_same(got, to_u256(want)));
    }
}

namespace {

std::vector<gpu_field_test_in> field_test_inputs() {
    rng r(0x6e0006);
    edge_values const edges;
    std::vector<fe8> vals = edges.v;
    for (int i = 0; i < 4096; ++i) vals.push_back(mpz_to_fe(r.below(P())));
    std::vector<gpu_field_test_in> in;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        gpu_field_test_in t{};
        fe_to_words(vals[i], t.a);
        fe_to_words(vals[(i * 13 + 7) % vals.size()], t.b);
        in.push_back(t);
    }
    for (fe8 const& a : edges.v) for (fe8 const& b : edges.v) {
        gpu_field_test_in t{};
        fe_to_words(a, t.a);
        fe_to_words(b, t.b);
        in.push_back(t);
    }
    return in;
}

void check_field_test(std::vector<gpu_field_test_in> const& in, std::vector<gpu_field_test_out> const& got) {
    REQUIRE(got.size() == in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        gpu_field_test_out want{};
        gpu::field_test_host(in[i], want);
        INFO("vector " << i);
        CHECK(std::memcmp(&want, &got[i], sizeof(want)) == 0);
    }
}

} // namespace

#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
TEST_CASE("cuda field arithmetic matches the host", "[gpu][cuda]") {
    if (!gpu::cuda_available()) { WARN("no CUDA device: skipped"); return; }
    auto const in = field_test_inputs();
    std::vector<gpu_field_test_out> got(in.size());
    REQUIRE(gpu::cuda_field_test(in.data(), in.size(), got.data()));
    check_field_test(in, got);
}
#endif

#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
TEST_CASE("metal field arithmetic matches the host", "[gpu][metal]") {
    // A device without a working kernel is a failure, not a skip.
    REQUIRE(gpu::metal_device_name().find("compile failed") == std::string::npos);
    if (!gpu::metal_available()) { WARN("no Metal device: skipped"); return; }
    auto const in = field_test_inputs();
    std::vector<gpu_field_test_out> got(in.size());
    REQUIRE(gpu::metal_field_test(in.data(), in.size(), got.data()));
    check_field_test(in, got);
}
#endif
