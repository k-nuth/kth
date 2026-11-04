// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// End to end: every backend must give exactly libsecp256k1's verdict for
// valid signatures, every kind of corruption, and structurally broken inputs.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include <kth/secp256k1_simd/batch_verify.hpp>

#include "helpers.hpp"

using namespace kth::secp256k1_simd;
using namespace kth::secp256k1_simd::test;

namespace {

secp256k1_context* ctx() {
    static secp256k1_context* c = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    return c;
}

struct item {
    std::vector<std::uint8_t> sig = std::vector<std::uint8_t>(64);
    bytes32 msg{};
    std::vector<std::uint8_t> pubkey;
    signature_input view() const { return {sig.data(), msg.data(), pubkey.data(), pubkey.size()}; }
};

enum class scheme { ecdsa, schnorr };

item make_valid(rng& r, scheme s, int pubkey_form) {
    item it;
    bytes32 seckey;
    do { seckey = r.b32(); } while (secp256k1_ec_seckey_verify(ctx(), seckey.data()) != 1);
    it.msg = r.b32();
    secp256k1_pubkey pk;
    REQUIRE(secp256k1_ec_pubkey_create(ctx(), &pk, seckey.data()) == 1);
    std::uint8_t ser[65]; std::size_t len = 65;
    if (pubkey_form == 0) {
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &pk, SECP256K1_EC_COMPRESSED);
    } else {
        secp256k1_ec_pubkey_serialize(ctx(), ser, &len, &pk, SECP256K1_EC_UNCOMPRESSED);
        if (pubkey_form == 2) ser[0] = 0x06 | (ser[64] & 1);  // hybrid
    }
    it.pubkey.assign(ser, ser + len);
    if (s == scheme::ecdsa) {
        secp256k1_ecdsa_signature sig;
        REQUIRE(secp256k1_ecdsa_sign(ctx(), &sig, it.msg.data(), seckey.data(), nullptr, nullptr) == 1);
        secp256k1_ecdsa_signature_serialize_compact(ctx(), it.sig.data(), &sig);
    } else {
        REQUIRE(secp256k1_schnorr_sign(ctx(), it.sig.data(), it.msg.data(), seckey.data(), nullptr, nullptr) == 1);
    }
    return it;
}

std::vector<backend> backends() {
    std::vector<backend> b = {backend::scalar_portable};
    if (backend_available(backend::avx512_ifma)) b.push_back(backend::avx512_ifma);
    return b;
}

std::size_t run(scheme s, std::vector<item> const& items, std::vector<std::uint8_t>& results, backend b) {
    std::vector<signature_input> in;
    for (auto const& it : items) in.push_back(it.view());
    results.assign(items.size(), 0xAA);
    return s == scheme::ecdsa ? verify_ecdsa(in, results, b) : verify_schnorr_bch(in, results, b);
}

void check_against_oracle(scheme s, std::vector<item> const& items) {
    std::vector<std::uint8_t> oracle;
    std::size_t const oracle_count = run(s, items, oracle, backend::secp256k1_fallback);
    for (backend b : backends()) {
        std::vector<std::uint8_t> got;
        std::size_t const count = run(s, items, got, b);
        INFO("backend " << backend_name(b));
        CHECK(count == oracle_count);
        REQUIRE(got.size() == oracle.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            INFO("item " << i);
            CHECK(got[i] == oracle[i]);
        }
    }
}

bytes32 n_minus(std::uint8_t const* s32) {
    mpz t = from_be32(s32);
    mpz_sub(t.v, N().v, t.v);
    return to_be32(t);
}

} // namespace

TEST_CASE("valid signatures verify on every backend", "[verify]") {
    rng r(0x5eed0030);
    for (scheme s : {scheme::ecdsa, scheme::schnorr}) {
        for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(7), std::size_t(8), std::size_t(9), std::size_t(17), std::size_t(64)}) {
            std::vector<item> items;
            for (std::size_t i = 0; i < n; ++i) items.push_back(make_valid(r, s, static_cast<int>(i % 3)));
            for (backend b : backends()) {
                std::vector<std::uint8_t> got;
                INFO("scheme " << (s == scheme::ecdsa ? "ecdsa" : "schnorr") << " n=" << n << " backend " << backend_name(b));
                CHECK(run(s, items, got, b) == n);
                for (auto v : got) CHECK(v == 1);
            }
            check_against_oracle(s, items);
        }
    }
}

TEST_CASE("corrupted and malformed inputs match libsecp256k1", "[verify]") {
    rng r(0x5eed0031);
    for (scheme s : {scheme::ecdsa, scheme::schnorr}) {
        std::vector<item> items;
        auto add = [&](item it) { items.push_back(std::move(it)); };
        for (int round = 0; round < 4; ++round) {
            item const base = make_valid(r, s, round % 3);
            add(base);
            { item it = base; it.sig[r.u64() % 64] ^= static_cast<std::uint8_t>(1 << (r.u64() % 8)); add(it); }  // bit flip
            { item it = base; it.msg[r.u64() % 32] ^= 1; add(it); }                                              // wrong message
            { item it = base; it.pubkey = make_valid(r, s, 0).pubkey; add(it); }                                 // wrong key
            { item it = base; bytes32 const ns = n_minus(it.sig.data() + 32); std::copy(ns.begin(), ns.end(), it.sig.begin() + 32); add(it); } // negated s (high-S for ECDSA; invalid for Schnorr)
            { item it = base; std::fill(it.sig.begin(), it.sig.begin() + 32, 0); add(it); }                     // r = 0
            { item it = base; std::fill(it.sig.begin() + 32, it.sig.end(), 0); add(it); }                       // s = 0
            { item it = base; bytes32 const nb = to_be32(N()); std::copy(nb.begin(), nb.end(), it.sig.begin() + 32); add(it); } // s = n
            { item it = base; bytes32 const nb = to_be32(N()); std::copy(nb.begin(), nb.end(), it.sig.begin()); add(it); }      // r = n
            { item it = base; std::fill(it.sig.begin(), it.sig.begin() + 32, 0xFF); add(it); }                  // r = 2^256-1 (>= p)
            { item it = base; bytes32 const pb = to_be32(P()); std::copy(pb.begin(), pb.end(), it.sig.begin()); add(it); }      // r = p
            { item it = base; it.pubkey[0] ^= 0x01; add(it); }                                                   // wrong parity / hybrid mismatch
            { item it = base; it.pubkey[0] = 0x05; add(it); }                                                    // bad prefix
            { item it = base; it.pubkey.resize(it.pubkey.size() - 1); add(it); }                                // bad length
            { item it = base; std::fill(it.pubkey.begin() + 1, it.pubkey.begin() + 33, 0xFF); add(it); }        // x >= p
            { item it = base; it.pubkey[1 + (r.u64() % 32)] ^= 0x10; add(it); }                                  // x off curve (compressed) / not on curve
            { item it = base; it.pubkey.resize(65); it.pubkey[0] = 0x04; bytes32 const x = r.b32(), y = r.b32(); std::copy(x.begin(), x.end(), it.pubkey.begin() + 1); std::copy(y.begin(), y.end(), it.pubkey.begin() + 33); add(it); } // random uncompressed
            { item it = base; bytes32 const g = r.b32(); std::copy(g.begin(), g.end(), it.sig.begin()); add(it); } // random r
            { item it = base; bytes32 const g = r.b32(); std::copy(g.begin(), g.end(), it.sig.begin() + 32); add(it); } // random s
            { item it = base; it.msg.fill(0); add(it); }                                                         // zero message
            { item it = base; it.msg.fill(0xFF); add(it); }                                                      // message >= n
        }
        check_against_oracle(s, items);
    }
}

TEST_CASE("mixed batches keep per-item verdicts in order", "[verify]") {
    rng r(0x5eed0032);
    for (scheme s : {scheme::ecdsa, scheme::schnorr}) {
        std::vector<item> items;
        std::vector<bool> expect;
        for (int i = 0; i < 100; ++i) {
            item it = make_valid(r, s, i % 3);
            bool const corrupt = (r.u64() % 3) == 0;
            if (corrupt) it.sig[r.u64() % 64] ^= 0x40;
            items.push_back(it);
            expect.push_back(!corrupt);
        }
        for (backend b : backends()) {
            std::vector<std::uint8_t> got;
            std::size_t count = run(s, items, got, b);
            std::size_t want = 0;
            for (std::size_t i = 0; i < items.size(); ++i) {
                INFO("backend " << backend_name(b) << " item " << i);
                CHECK((got[i] == 1) == expect[i]);
                want += expect[i];
            }
            CHECK(count == want);
        }
        check_against_oracle(s, items);
    }
}

TEST_CASE("algebraic Schnorr batch agrees with per-signature verdicts", "[verify][algebraic]") {
    rng r(0x5eed0033);
    bytes32 const seed = r.b32();
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(7), std::size_t(8), std::size_t(9), std::size_t(64), std::size_t(200)}) {
        std::vector<item> items;
        for (std::size_t i = 0; i < n; ++i) items.push_back(make_valid(r, scheme::schnorr, static_cast<int>(i % 3)));
        std::vector<signature_input> in;
        for (auto const& it : items) in.push_back(it.view());
        INFO("n=" << n);
        CHECK(verify_schnorr_bch_all(in, seed.data()));
        CHECK(verify_schnorr_bch_all(in, nullptr));
        if (n == 0) continue;
        // Corrupt one item at a random position: the batch must fail, and
        // the oracle must agree.
        std::size_t const victim = r.u64() % n;
        item corrupted = items[victim];
        switch (r.u64() % 4) {
            case 0: corrupted.sig[r.u64() % 64] ^= 0x08; break;
            case 1: corrupted.msg[r.u64() % 32] ^= 0x01; break;
            case 2: corrupted.pubkey = make_valid(r, scheme::schnorr, 0).pubkey; break;
            default: { bytes32 const ns = n_minus(corrupted.sig.data() + 32); std::copy(ns.begin(), ns.end(), corrupted.sig.begin() + 32); break; }
        }
        in[victim] = corrupted.view();
        std::vector<std::uint8_t> oracle(n);
        std::vector<item> mixed = items; mixed[victim] = corrupted;
        std::size_t const valid = run(scheme::schnorr, mixed, oracle, backend::secp256k1_fallback);
        CHECK(valid == n - 1);
        CHECK_FALSE(verify_schnorr_bch_all(in, seed.data()));
        // Malformed public key: parse fails, batch fails.
        in[victim] = items[victim].view();
        item bad = items[victim]; bad.pubkey[0] = 0x05;
        in[victim] = bad.view();
        CHECK_FALSE(verify_schnorr_bch_all(in, seed.data()));
    }
}
