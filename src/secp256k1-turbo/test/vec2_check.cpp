// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// A check of a two-lane backend with no dependency beyond libsecp256k1.
//
// The Catch2 suite compares the field against GMP and is not built for
// WebAssembly, which is where the SIMD128 lanes have to be checked. This
// program asks the same two questions without either library:
//
//   1. the field, lane by lane, against the portable one-lane backend, limb
//      for limb;
//   2. the public API with this backend against libsecp256k1, on valid,
//      corrupted and malformed signatures.
//
// It prints what it compared and exits non-zero on the first disagreement.
// The lanes are whichever two-lane type the target has, so the same program
// checks NEON on AArch64 — which is how it is exercised outside a browser
// engine.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include <secp256k1_turbo/batch_verify.hpp>

#include "fe52.hpp"
#include "vec_scalar.hpp"

#if defined(__wasm_simd128__)
#include "vec_wasm_simd128.hpp"
constexpr secp256k1_turbo::backend lanes_backend = secp256k1_turbo::backend::wasm_simd128;
#elif defined(__aarch64__)
#include "vec_neon.hpp"
constexpr secp256k1_turbo::backend lanes_backend = secp256k1_turbo::backend::neon;
#else
#error "vec2_check needs a target with a two-lane backend"
#endif

using namespace secp256k1_turbo;

namespace {

using F2 = field<vec2>;
using F1 = field<vec1>;

[[noreturn]] void fail(char const* what, int iteration, int lane) {
    std::fprintf(stderr, "FAIL: %s (iteration %d, lane %d)\n", what, iteration, lane);
    std::exit(1);
}

struct rng {
    std::mt19937_64 g;
    explicit rng(std::uint64_t seed) : g(seed) {}
    // Random limbs below 2^52: any value below 2^260, canonical or not.
    fe<vec1> loose() {
        fe<vec1> r;
        for (auto& l : r.n) l.v = g() & fe_const::M52;
        return r;
    }
    // The same, reduced: a canonical element.
    fe<vec1> canonical() {
        fe<vec1> r = loose();
        F1::normalize(r);
        return r;
    }
    std::array<std::uint8_t, 32> b32() {
        std::array<std::uint8_t, 32> r;
        for (auto& x : r) x = static_cast<std::uint8_t>(g());
        return r;
    }
};

fe<vec2> pack(fe<vec1> const (&a)[2]) {
    std::uint64_t limbs[5][2];
    for (int l = 0; l < 2; ++l) for (int i = 0; i < 5; ++i) limbs[i][l] = a[l].n[i].v;
    return F2::load(&limbs[0][0], 2);
}

void same(fe<vec2> const& got, fe<vec1> const& want, int lane, char const* what, int iteration) {
    for (int i = 0; i < 5; ++i) {
        if (got.n[i].lane(std::size_t(lane)) != want.n[i].v) fail(what, iteration, lane);
    }
}

int check_field() {
    rng r(0x5eed2026);
    int const iterations = 2000;
    for (int iter = 0; iter < iterations; ++iter) {
        fe<vec1> a[2], b[2];
        for (int l = 0; l < 2; ++l) {
            a[l] = ((iter + l) & 1) ? r.loose() : r.canonical();
            b[l] = ((iter + l) & 2) ? r.loose() : r.canonical();
        }
        fe<vec2> const A = pack(a), B = pack(b);
        fe<vec2> const mul = F2::mul(A, B), sqr = F2::sqr(A), add = F2::add(A, B),
                       sub = F2::sub(A, B), half = F2::half(A);
        fe<vec2> nrm = A; F2::normalize(nrm);
        vec2 const eqm = F2::eq(A, B), eqs = F2::eq(A, A), odd = F2::is_odd(A);
        for (int l = 0; l < 2; ++l) {
            same(mul, F1::mul(a[l], b[l]), l, "field multiply", iter);
            same(sqr, F1::sqr(a[l]), l, "field square", iter);
            same(add, F1::add(a[l], b[l]), l, "field add", iter);
            same(sub, F1::sub(a[l], b[l]), l, "field subtract", iter);
            same(half, F1::half(a[l]), l, "field halve", iter);
            fe<vec1> n1 = a[l]; F1::normalize(n1);
            same(nrm, n1, l, "field normalize", iter);
            if (eqm.lane(std::size_t(l)) != F1::eq(a[l], b[l]).v) fail("field equality", iter, l);
            if (eqs.lane(std::size_t(l)) != ~std::uint64_t(0)) fail("field equality with itself", iter, l);
            if (odd.lane(std::size_t(l)) != F1::is_odd(a[l]).v) fail("field parity", iter, l);
        }
    }
    // The long chains, fewer times: an inverse or a square root is hundreds
    // of multiplies, so one wrong carry anywhere shows here.
    int const chains = 40;
    for (int iter = 0; iter < chains; ++iter) {
        fe<vec1> a[2] = {r.canonical(), r.canonical()};
        fe<vec2> const A = pack(a);
        fe<vec2> const inv = F2::inverse(A), root = F2::sqrt_candidate(A);
        vec2 const square = F2::is_square_nonzero(A);
        for (int l = 0; l < 2; ++l) {
            same(inv, F1::inverse(a[l]), l, "field inverse", iter);
            same(root, F1::sqrt_candidate(a[l]), l, "field square root", iter);
            if (square.lane(std::size_t(l)) != F1::is_square_nonzero(a[l]).v) fail("field is-square", iter, l);
        }
    }
    return iterations + chains;
}

struct item {
    std::array<std::uint8_t, 64> sig{};
    std::array<std::uint8_t, 32> msg{};
    std::vector<std::uint8_t> pubkey;
    signature_input view() const { return {sig.data(), msg.data(), pubkey.data(), pubkey.size()}; }
};

item make_valid(secp256k1_context* ctx, rng& r, bool schnorr, int pubkey_form) {
    item it;
    std::array<std::uint8_t, 32> seckey;
    do { seckey = r.b32(); } while (secp256k1_ec_seckey_verify(ctx, seckey.data()) != 1);
    it.msg = r.b32();
    secp256k1_pubkey pk;
    if (secp256k1_ec_pubkey_create(ctx, &pk, seckey.data()) != 1) fail("public key creation", 0, 0);
    std::uint8_t ser[65]; std::size_t len = 65;
    if (pubkey_form == 0) {
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, ser, &len, &pk, SECP256K1_EC_COMPRESSED);
    } else {
        secp256k1_ec_pubkey_serialize(ctx, ser, &len, &pk, SECP256K1_EC_UNCOMPRESSED);
        if (pubkey_form == 2) ser[0] = 0x06 | (ser[64] & 1);  // hybrid
    }
    it.pubkey.assign(ser, ser + len);
    if (schnorr) {
        if (secp256k1_schnorr_sign(ctx, it.sig.data(), it.msg.data(), seckey.data(), nullptr, nullptr) != 1) {
            fail("Schnorr signing", 0, 0);
        }
    } else {
        secp256k1_ecdsa_signature sig;
        if (secp256k1_ecdsa_sign(ctx, &sig, it.msg.data(), seckey.data(), nullptr, nullptr) != 1) {
            fail("ECDSA signing", 0, 0);
        }
        secp256k1_ecdsa_signature_serialize_compact(ctx, it.sig.data(), &sig);
    }
    return it;
}

// Valid signatures with a share of them damaged in each of the ways a
// verifier has to refuse: a flipped signature bit, a flipped message bit, a
// public key that is not on the curve, and one with an unknown prefix.
int check_verify(secp256k1_context* ctx, bool schnorr) {
    rng r(schnorr ? 0x5eed5c40 : 0x5eedec05);
    std::size_t const count = 512;   // not a multiple of the lane count plus tails below
    std::vector<item> items;
    items.reserve(count + 3);
    for (std::size_t i = 0; i < count + 3; ++i) {
        item it = make_valid(ctx, r, schnorr, int(i % 3));
        switch (i % 7) {
            case 1: it.sig[r.g() % 64] ^= std::uint8_t(1u << (r.g() % 8)); break;
            case 3: it.msg[r.g() % 32] ^= std::uint8_t(1u << (r.g() % 8)); break;
            case 5: it.pubkey[1 + r.g() % 32] ^= std::uint8_t(1u << (r.g() % 8)); break;
            case 6: if (i % 21 == 6) it.pubkey[0] = 0x05; break;
            default: break;
        }
        items.push_back(std::move(it));
    }

    int compared = 0;
    // Every prefix length around the lane boundaries, then the whole batch.
    for (std::size_t n : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(7), std::size_t(8),
                          std::size_t(9), std::size_t(16), std::size_t(17), items.size()}) {
        std::vector<signature_input> in;
        for (std::size_t i = 0; i < n; ++i) in.push_back(items[i].view());
        std::vector<std::uint8_t> want(n), got(n);
        std::size_t const want_valid = schnorr ? verify_schnorr_bch(in, want, backend::secp256k1_fallback)
                                               : verify_ecdsa(in, want, backend::secp256k1_fallback);
        std::size_t const got_valid = schnorr ? verify_schnorr_bch(in, got, lanes_backend)
                                              : verify_ecdsa(in, got, lanes_backend);
        if (got_valid != want_valid) fail(schnorr ? "Schnorr valid count" : "ECDSA valid count", int(n), 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (got[i] != want[i]) fail(schnorr ? "Schnorr verdict" : "ECDSA verdict", int(n), int(i));
        }
        // Not everything valid and not everything refused: a batch that says
        // one thing about every signature would agree with anything.
        if (n == items.size() && (want_valid == 0 || want_valid == n)) {
            fail("the batch does not mix valid and invalid signatures", int(n), 0);
        }
        compared += int(n);
    }
    return compared;
}

// Nanoseconds per signature for one backend over a batch, best of a few runs.
// A rough figure, to place the lanes next to libsecp256k1 on this engine; the
// native benchmark is the instrument where one can be built.
double time_backend(std::vector<signature_input> const& in, bool schnorr, backend b) {
    std::vector<std::uint8_t> out(in.size());
    double best = 0;
    for (int run = 0; run < 5; ++run) {
        auto const t0 = std::chrono::steady_clock::now();
        std::size_t const valid = schnorr ? verify_schnorr_bch(in, out, b) : verify_ecdsa(in, out, b);
        auto const t1 = std::chrono::steady_clock::now();
        if (valid != in.size()) fail("a timed batch held an invalid signature", run, 0);
        double const ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / double(in.size());
        if (run == 0 || ns < best) best = ns;
    }
    return best;
}

void report_timings(secp256k1_context* ctx) {
    for (bool const schnorr : {false, true}) {
        rng r(schnorr ? 0x7153c401 : 0x7153ec01);
        std::vector<item> items;
        for (int i = 0; i < 2048; ++i) items.push_back(make_valid(ctx, r, schnorr, 0));
        std::vector<signature_input> in;
        for (auto const& it : items) in.push_back(it.view());
        std::printf("%s, batch of 2048, ns per signature:\n", schnorr ? "Schnorr (BCH)" : "ECDSA");
        for (backend const b : {backend::secp256k1_fallback, backend::scalar_portable, lanes_backend}) {
            std::printf("  %-36s %10.0f\n", std::string(backend_name(b)).c_str(), time_backend(in, schnorr, b));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if ( ! backend_available(lanes_backend)) {
        std::fprintf(stderr, "FAIL: the two-lane backend is not available in this build\n");
        return 1;
    }
    if (lanes(lanes_backend) != 2) {
        std::fprintf(stderr, "FAIL: the two-lane backend reports %zu lanes\n", lanes(lanes_backend));
        return 1;
    }
    std::printf("backend: %s\n", std::string(backend_name(lanes_backend)).c_str());

    int const field_cases = check_field();
    std::printf("field: %d cases, each lane equal to the portable backend\n", field_cases);

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    int const ecdsa = check_verify(ctx, false);
    std::printf("ECDSA: %d verdicts equal to libsecp256k1\n", ecdsa);
    int const schnorr = check_verify(ctx, true);
    std::printf("Schnorr (BCH): %d verdicts equal to libsecp256k1\n", schnorr);
    std::printf("PASS\n");

    if (argc > 1 && std::string(argv[1]) == "time") {
        report_timings(ctx);
    }
    secp256k1_context_destroy(ctx);
    return 0;
}
