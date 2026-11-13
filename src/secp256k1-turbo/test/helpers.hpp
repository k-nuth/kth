// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SECP256K1_TURBO_TEST_HELPERS_HPP
#define KTH_SECP256K1_TURBO_TEST_HELPERS_HPP

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <gmp.h>

#include "fe52.hpp"
#include "scalar256.hpp"
#include "vec_scalar.hpp"

namespace secp256k1_turbo::test {

using bytes32 = std::array<std::uint8_t, 32>;

struct mpz {
    mpz_t v;
    mpz() { mpz_init(v); }
    explicit mpz(char const* hex) { mpz_init_set_str(v, hex, 16); }
    mpz(mpz const& o) { mpz_init_set(v, o.v); }
    mpz& operator=(mpz const& o) { mpz_set(v, o.v); return *this; }
    ~mpz() { mpz_clear(v); }
};

inline mpz const& P() { static mpz const p("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F"); return p; }
inline mpz const& N() { static mpz const n("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141"); return n; }

inline mpz from_be32(std::uint8_t const* b) { mpz r; mpz_import(r.v, 32, 1, 1, 1, 0, b); return r; }
inline bytes32 to_be32(mpz const& a) {
    bytes32 out{};
    std::size_t count = 0;
    std::uint8_t tmp[32] = {};
    mpz_export(tmp, &count, 1, 1, 1, 0, a.v);
    for (std::size_t i = 0; i < count; ++i) out[32 - count + i] = tmp[i];
    return out;
}

inline mpz from_limbs52(std::uint64_t const l[5]) {
    mpz r;
    for (int i = 4; i >= 0; --i) {
        mpz_mul_2exp(r.v, r.v, 52);
        mpz_add_ui(r.v, r.v, 0);
        mpz t; mpz_set_ui(t.v, 0);
        mpz_import(t.v, 1, 1, 8, 0, 0, &l[i]);
        mpz_add(r.v, r.v, t.v);
    }
    return r;
}
inline mpz from_u256(u256 const& a) { mpz r; mpz_import(r.v, 4, -1, 8, 0, 0, a.d); return r; }
inline u256 to_u256(mpz const& a) {
    u256 r{}; std::size_t count = 0;
    mpz_export(r.d, &count, -1, 8, 0, 0, a.v);
    return r;
}

inline mpz fe1_to_mpz(fe<vec1> const& a) {
    std::uint64_t l[5];
    for (int i = 0; i < 5; ++i) l[i] = a.n[i].v;
    mpz r = from_limbs52(l);
    mpz_mod(r.v, r.v, P().v);
    return r;
}
inline fe<vec1> mpz_to_fe1(mpz const& a) {
    bytes32 const b = to_be32(a);
    std::uint64_t l[5];
    fe_limbs_from_be32(b.data(), l);
    return field<vec1>::from_limbs(l);
}

struct rng {
    std::mt19937_64 g;
    explicit rng(std::uint64_t seed) : g(seed) {}
    std::uint64_t u64() { return g(); }
    bytes32 b32() { bytes32 r; for (auto& x : r) x = static_cast<std::uint8_t>(g()); return r; }
    // Uniform in [0, m).
    mpz below(mpz const& m) {
        bytes32 const b = b32();
        mpz r = from_be32(b.data());
        mpz_mod(r.v, r.v, m.v);
        return r;
    }
    // Non-canonical field element: random limbs below 2^52 (value < 2^260).
    fe<vec1> fe_loose() {
        fe<vec1> r;
        for (auto& l : r.n) l.v = g() & fe_const::M52;
        return r;
    }
};

} // namespace secp256k1_turbo::test

#endif
