// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "g_table.hpp"

#include <memory>
#include <vector>

#include "group.hpp"
#include "vec_scalar.hpp"

namespace secp256k1_turbo {

namespace {

constexpr std::uint8_t G_X_BE[32] = {
    0x79,0xBE,0x66,0x7E,0xF9,0xDC,0xBB,0xAC,0x55,0xA0,0x62,0x95,0xCE,0x87,0x0B,0x07,
    0x02,0x9B,0xFC,0xDB,0x2D,0xCE,0x28,0xD9,0x59,0xF2,0x81,0x5B,0x16,0xF8,0x17,0x98};
constexpr std::uint8_t G_Y_BE[32] = {
    0x48,0x3A,0xDA,0x77,0x26,0xA3,0xC4,0x65,0x5D,0xA4,0xFB,0xFC,0x0E,0x11,0x08,0xA8,
    0xFD,0x17,0xB4,0x48,0xA6,0x85,0x54,0x19,0x9C,0x47,0xD0,0x8F,0xFB,0x10,0xD4,0xB8};

std::uint64_t limbs_of(std::uint8_t const* be, int i) {
    std::uint64_t l[5];
    fe_limbs_from_be32(be, l);
    return l[i];
}

std::unique_ptr<g_table> build() {
    using V = vec1;
    using Fd = field<V>;
    using Gr = group<V>;

    auto table = std::make_unique<g_table>();
    ge<V> g;
    g.x = Fd::from_limbs(G_X);
    g.y = Fd::from_limbs(G_Y);
    // h = 2^128 * G
    gej<V> hj = Gr::from_ge(g);
    for (int i = 0; i < 128; ++i) hj = Gr::dbl(hj);
    ge<V> const h = Gr::to_affine(hj);

    // jac[d - 1] = c * G + c' * h for d = c + 16 c' in 1..255
    std::vector<gej<V>> jac(g_table::entries - 1);
    for (int c2 = 0; c2 < 16; ++c2) {
        gej<V> row = Gr::infinity();  // c' * h
        for (int i = 0; i < c2; ++i) row = Gr::add_ge(row, h);
        for (int c = 0; c < 16; ++c) {
            int const d = c + 16 * c2;
            if (d == 0) continue;
            gej<V> e = row;
            for (int i = 0; i < c; ++i) e = Gr::add_ge(e, g);
            jac[d - 1] = e;
        }
    }
    std::vector<ge<V>> aff(jac.size());
    std::vector<fe<V>> scratch(jac.size());
    Gr::batch_to_affine(jac.data(), aff.data(), jac.size(), scratch.data());

    for (int limb = 0; limb < g_table::limbs; ++limb) table->t[0][limb] = 0;
    for (int d = 1; d < g_table::entries; ++d) {
        ge<V> p = aff[d - 1];
        Fd::normalize(p.x);
        Fd::normalize(p.y);
        for (int i = 0; i < 5; ++i) {
            table->t[d][i] = p.x.n[i].v;
            table->t[d][5 + i] = p.y.n[i].v;
        }
    }
    return table;
}

} // namespace

std::uint64_t const G_X[5] = {limbs_of(G_X_BE, 0), limbs_of(G_X_BE, 1), limbs_of(G_X_BE, 2), limbs_of(G_X_BE, 3), limbs_of(G_X_BE, 4)};
std::uint64_t const G_Y[5] = {limbs_of(G_Y_BE, 0), limbs_of(G_Y_BE, 1), limbs_of(G_Y_BE, 2), limbs_of(G_Y_BE, 3), limbs_of(G_Y_BE, 4)};

g_table const& get_g_table() {
    static std::unique_ptr<g_table> const table = build();
    return *table;
}

} // namespace secp256k1_turbo
