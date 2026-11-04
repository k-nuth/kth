// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/secp256k1_simd/batch_verify.hpp>

#include <cstring>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include "cpu.hpp"
#include "fe52.hpp"
#include "g_table.hpp"
#include "kernel.hpp"
#include "scalar256.hpp"
#include "sha256.hpp"

namespace kth::secp256k1_simd {

namespace {

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
bool parse_pubkey(std::uint8_t const* pk, std::size_t size, prepared& p) {
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

void prepare_ecdsa(signature_input const& in, prepared& p) {
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

void prepare_schnorr(signature_input const& in, prepared& p) {
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

// s^-1 for every valid ECDSA item with one modular inversion (Montgomery's trick).
void finish_ecdsa_scalars(std::vector<prepared>& items) {
    std::vector<std::size_t> idx;
    idx.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) if (items[i].ok) idx.push_back(i);
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

void fill_lane(lane_batch& b, std::size_t lane, prepared const& p) {
    for (int i = 0; i < 5; ++i) {
        b.qx[i][lane] = p.qx[i];
        b.qy[i][lane] = p.qy[i];
        b.r[i][lane] = p.r[i];
        b.rn[i][lane] = p.rn[i];
    }
    b.decompress[lane] = p.decompress ? ~std::uint64_t(0) : 0;
    b.parity_odd[lane] = p.parity_odd ? ~std::uint64_t(0) : 0;
    b.use_rn[lane] = p.use_rn ? ~std::uint64_t(0) : 0;
    lane_set_scalars(b, lane, p.u1, p.u2);
}

// A structurally valid but irrelevant lane (its verdict is discarded).
prepared benign_lane() {
    prepared p;
    for (int i = 0; i < 5; ++i) { p.qx[i] = G_X[i]; p.qy[i] = G_Y[i]; p.r[i] = G_X[i]; }
    p.decompress = false;
    p.parity_odd = (G_Y[0] & 1) != 0;
    p.u1 = sn::ONE;
    p.u2 = sn::ZERO;
    p.ok = true;
    return p;
}

std::size_t run_lanes(std::vector<prepared> const& items, std::span<std::uint8_t> results, check_mode mode, kernel_fn kernel) {
    prepared const benign = benign_lane();
    std::size_t valid = 0;
    lane_batch b;
    for (std::size_t base = 0; base < items.size(); base += max_lanes) {
        std::size_t const count = (items.size() - base < max_lanes) ? items.size() - base : max_lanes;
        for (std::size_t lane = 0; lane < max_lanes; ++lane) {
            prepared const& p = (lane < count && items[base + lane].ok) ? items[base + lane] : benign;
            fill_lane(b, lane, p);
        }
        kernel(b, mode);
        for (std::size_t lane = 0; lane < count; ++lane) {
            bool const v = items[base + lane].ok && b.valid[lane] != 0;
            results[base + lane] = v ? 1 : 0;
            valid += v;
        }
    }
    return valid;
}

std::size_t run_fallback(std::span<signature_input const> inputs, std::span<std::uint8_t> results, check_mode mode) {
    secp256k1_context const* ctx = secp256k1_context_static;
    std::size_t valid = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        signature_input const& in = inputs[i];
        secp256k1_pubkey pk;
        bool v = secp256k1_ec_pubkey_parse(ctx, &pk, in.pubkey, in.pubkey_size) == 1;
        if (v) {
            if (mode == check_mode::ecdsa) {
                secp256k1_ecdsa_signature sig;
                v = secp256k1_ecdsa_signature_parse_compact(ctx, &sig, in.sig64) == 1 &&
                    secp256k1_ecdsa_verify(ctx, &sig, in.msg32, &pk) == 1;
            } else {
                v = secp256k1_schnorr_verify(ctx, in.sig64, in.msg32, &pk) == 1;
            }
        }
        results[i] = v ? 1 : 0;
        valid += v;
    }
    return valid;
}

kernel_fn kernel_for(backend b) {
    switch (b) {
        case backend::scalar_portable: return &kernel_scalar;
#if defined(KTH_SECP256K1_SIMD_HAVE_AVX512IFMA)
        case backend::avx512_ifma: return &kernel_avx512ifma;
#endif
        default: return nullptr;
    }
}

// Below this many signatures a vector kernel run (8 lanes, mostly idle) costs
// more than verifying them one at a time with libsecp256k1.
constexpr std::size_t min_vector_batch = 2;

std::size_t verify(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b, check_mode mode) {
    if (!backend_available(b)) b = best_backend();
    if (b == backend::avx512_ifma && inputs.size() < min_vector_batch) b = backend::secp256k1_fallback;
    kernel_fn const kernel = kernel_for(b);
    if (kernel == nullptr) return run_fallback(inputs, results, mode);
    std::vector<prepared> items(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (mode == check_mode::ecdsa) prepare_ecdsa(inputs[i], items[i]);
        else prepare_schnorr(inputs[i], items[i]);
    }
    if (mode == check_mode::ecdsa) finish_ecdsa_scalars(items);
    return run_lanes(items, results, mode, kernel);
}

} // namespace

bool backend_available(backend b) {
    switch (b) {
        case backend::secp256k1_fallback: return true;
        case backend::scalar_portable: return true;
        case backend::avx512_ifma:
#if defined(KTH_SECP256K1_SIMD_HAVE_AVX512IFMA) && defined(__AVX512IFMA__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__AVX512BW__)
            // The whole build targets an IFMA microarchitecture: static choice, no CPUID.
            return true;
#elif defined(KTH_SECP256K1_SIMD_HAVE_AVX512IFMA)
            { static bool const ok = cpu_supports_avx512ifma(); return ok; }
#else
            return false;
#endif
    }
    return false;
}

backend best_backend() {
    if (backend_available(backend::avx512_ifma)) return backend::avx512_ifma;
    return backend::secp256k1_fallback;
}

std::string_view backend_name(backend b) {
    switch (b) {
        case backend::secp256k1_fallback: return "secp256k1 (scalar, per signature)";
        case backend::scalar_portable: return "portable 1-lane";
        case backend::avx512_ifma: return "avx512-ifma 8-lane";
    }
    return "?";
}

std::size_t lanes(backend b) { return b == backend::avx512_ifma ? 8 : 1; }

std::size_t verify_ecdsa(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b) {
    return verify(inputs, results, b, check_mode::ecdsa);
}

std::size_t verify_schnorr_bch(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b) {
    return verify(inputs, results, b, check_mode::schnorr_bch);
}

bool verify_schnorr_bch_all(std::span<signature_input const> inputs, std::uint8_t const* seed32) {
    secp256k1_context const* ctx = secp256k1_context_static;
    std::vector<secp256k1_pubkey> keys(inputs.size());
    std::vector<secp256k1_pubkey const*> key_ptrs(inputs.size());
    std::vector<std::uint8_t const*> sigs(inputs.size());
    std::vector<std::uint8_t const*> msgs(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (secp256k1_ec_pubkey_parse(ctx, &keys[i], inputs[i].pubkey, inputs[i].pubkey_size) != 1) return false;
        key_ptrs[i] = &keys[i];
        sigs[i] = inputs[i].sig64;
        msgs[i] = inputs[i].msg32;
    }
    return secp256k1_schnorr_verify_batch(ctx, sigs.data(), msgs.data(), key_ptrs.data(), inputs.size(), seed32) == 1;
}

} // namespace kth::secp256k1_simd
