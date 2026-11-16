// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Per-signature scalar preprocessing shared by every backend (CPU lanes and
// GPU): public key parsing, signature range checks, the ECDSA scalars
// u1 = z / s and u2 = r / s (with one batched inversion) and the BCH Schnorr
// challenge. Everything here runs on the host before the kernels.

#ifndef KTH_SECP256K1_TURBO_PREPARE_HPP
#define KTH_SECP256K1_TURBO_PREPARE_HPP

#include <cstdint>
#include <cstring>
#include <vector>

#include <secp256k1_turbo/batch_verify.hpp>

#include "fe52.hpp"
#include "scalar256.hpp"
#include "sha256.hpp"

namespace secp256k1_turbo {

// Per-signature scalar preprocessing, before anything is laid out in lanes.
struct prepared {
    bool ok = false;               // passed every structural check
    std::uint64_t qx[5] = {};      // field limbs
    std::uint64_t qy[5] = {};      // only when !decompress
    bool decompress = false;
    bool parity_odd = false;
    u256 u1 = sn::ZERO;            // G scalar
    u256 u2 = sn::ZERO;            // Q scalar
    std::uint64_t r[5] = {};       // r as field limbs
    std::uint64_t rn[5] = {};      // r + n as field limbs (ECDSA)
    bool use_rn = false;
    u256 s_mont = sn::ZERO;        // ECDSA: s in Montgomery form (for the batch inversion)
    u256 z = sn::ZERO;             // ECDSA: message scalar
    u256 rs = sn::ZERO;            // ECDSA: r as scalar
    std::uint8_t compressed[33];   // Schnorr: compressed pubkey for the challenge
};

// Mirrors secp256k1_ec_pubkey_parse: 02/03 compressed, 04 uncompressed, 06/07 hybrid.
inline bool parse_pubkey(std::uint8_t const* pk, std::size_t size, prepared& p) {
    if (size == 33 && (pk[0] == 0x02 || pk[0] == 0x03)) {
        if (!be32_less_than_p(pk + 1)) return false;
        fe_limbs_from_be32(pk + 1, p.qx);
        p.decompress = true;
        p.parity_odd = pk[0] == 0x03;
        std::memcpy(p.compressed, pk, 33);
        return true;
    }
    if (size == 65 && (pk[0] == 0x04 || pk[0] == 0x06 || pk[0] == 0x07)) {
        if (!be32_less_than_p(pk + 1) || !be32_less_than_p(pk + 33)) return false;
        bool const odd = (pk[64] & 1) != 0;
        if (pk[0] != 0x04 && odd != (pk[0] == 0x07)) return false;
        fe_limbs_from_be32(pk + 1, p.qx);
        fe_limbs_from_be32(pk + 33, p.qy);
        p.decompress = false;
        p.parity_odd = odd;
        p.compressed[0] = odd ? 0x03 : 0x02;
        std::memcpy(p.compressed + 1, pk + 1, 32);
        return true;
    }
    return false;
}

// n as a 256-bit integer, for r + n.
constexpr std::uint8_t N_BE[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41};

inline void prepare_ecdsa(signature_input const& in, prepared& p) {
    p.ok = false;
    if (!parse_pubkey(in.pubkey, in.pubkey_size, p)) return;
    u256 r, s;
    if (!sn::from_be32(in.sig64, r) || !sn::from_be32(in.sig64 + 32, s)) return;  // overflow
    if (sn::is_zero(r) || sn::is_zero(s)) return;
    if (sn::lt(sn::N_HALF, s)) return;  // high-S, rejected like secp256k1_ecdsa_verify
    sn::from_be32(in.msg32, p.z);        // reduced mod n, overflow allowed
    p.rs = r;
    p.s_mont = sn::to_mont(s);
    // r as a field element (r < n < p), and r + n when that is still < p.
    fe_limbs_from_be32(in.sig64, p.r);
    std::uint8_t rn_be[32];
    unsigned carry = 0;
    for (int i = 31; i >= 0; --i) {
        unsigned const t = static_cast<unsigned>(in.sig64[i]) + N_BE[i] + carry;
        rn_be[i] = static_cast<std::uint8_t>(t);
        carry = t >> 8;
    }
    p.use_rn = carry == 0 && be32_less_than_p(rn_be);
    if (p.use_rn) fe_limbs_from_be32(rn_be, p.rn);
    p.ok = true;
}

inline void prepare_schnorr(signature_input const& in, prepared& p) {
    p.ok = false;
    if (!parse_pubkey(in.pubkey, in.pubkey_size, p)) return;
    u256 s;
    if (!sn::from_be32(in.sig64 + 32, s)) return;    // s >= n
    if (!be32_less_than_p(in.sig64)) return;         // r >= p
    fe_limbs_from_be32(in.sig64, p.r);
    p.use_rn = false;
    // e = SHA256(r || compressed(P) || msg) mod n
    std::uint8_t buf[97];
    std::memcpy(buf, in.sig64, 32);
    std::memcpy(buf + 32, p.compressed, 33);
    std::memcpy(buf + 65, in.msg32, 32);
    std::uint8_t h[32];
    sha256(buf, 97, h);
    u256 e;
    sn::from_be32(h, e);
    p.u1 = s;
    p.u2 = sn::neg_mod(e);
    p.ok = true;
}

// s^-1 for every valid ECDSA item in [b, e) with one modular inversion (Montgomery's trick).
inline void finish_ecdsa_scalars(std::vector<prepared>& items, std::size_t b, std::size_t e) {
    std::vector<std::size_t> idx;
    idx.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) if (items[i].ok) idx.push_back(i);
    if (idx.empty()) return;
    std::vector<u256> prefix(idx.size());
    prefix[0] = items[idx[0]].s_mont;
    for (std::size_t k = 1; k < idx.size(); ++k) prefix[k] = sn::mont_mul(prefix[k - 1], items[idx[k]].s_mont);
    u256 inv = sn::inverse_mont(prefix.back());
    for (std::size_t k = idx.size(); k-- > 0;) {
        prepared& p = items[idx[k]];
        u256 const s_inv = (k == 0) ? inv : sn::mont_mul(inv, prefix[k - 1]);   // Montgomery form of s^-1
        if (k != 0) inv = sn::mont_mul(inv, p.s_mont);
        p.u1 = sn::mont_mul(p.z, s_inv);   // z * s^-1 (plain form)
        p.u2 = sn::mont_mul(p.rs, s_inv);  // r * s^-1
    }
}
inline void finish_ecdsa_scalars(std::vector<prepared>& items) { finish_ecdsa_scalars(items, 0, items.size()); }

} // namespace secp256k1_turbo

#endif
