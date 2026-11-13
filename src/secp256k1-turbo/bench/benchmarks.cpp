// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Throughput of signature verification: libsecp256k1 one signature at a
// time (what consensus does today) versus the lane-parallel batch kernels.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <array>
#include <cstdio>
#include <random>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include <secp256k1_turbo/batch_verify.hpp>

using namespace secp256k1_turbo;
using ankerl::nanobench::Bench;

namespace {

struct item {
    std::array<std::uint8_t, 64> sig;
    std::array<std::uint8_t, 32> msg;
    std::array<std::uint8_t, 33> pubkey;
    signature_input view() const { return {sig.data(), msg.data(), pubkey.data(), pubkey.size()}; }
};

secp256k1_context* ctx = nullptr;
std::uint8_t seed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

std::vector<item> make_items(std::size_t n, bool schnorr, std::uint64_t seed) {
    std::mt19937_64 g(seed);
    std::vector<item> items(n);
    for (auto& it : items) {
        std::array<std::uint8_t, 32> seckey;
        do { for (auto& b : seckey) b = static_cast<std::uint8_t>(g()); } while (secp256k1_ec_seckey_verify(ctx, seckey.data()) != 1);
        for (auto& b : it.msg) b = static_cast<std::uint8_t>(g());
        secp256k1_pubkey pk;
        if (secp256k1_ec_pubkey_create(ctx, &pk, seckey.data()) != 1) continue;
        std::size_t len = 33;
        secp256k1_ec_pubkey_serialize(ctx, it.pubkey.data(), &len, &pk, SECP256K1_EC_COMPRESSED);
        if (schnorr) {
            secp256k1_schnorr_sign(ctx, it.sig.data(), it.msg.data(), seckey.data(), nullptr, nullptr);
        } else {
            secp256k1_ecdsa_signature sig;
            secp256k1_ecdsa_sign(ctx, &sig, it.msg.data(), seckey.data(), nullptr, nullptr);
            secp256k1_ecdsa_signature_serialize_compact(ctx, it.sig.data(), &sig);
        }
    }
    return items;
}

// The per-signature path as consensus runs it: parse the key and the signature, verify.
std::size_t verify_one_by_one(std::vector<item> const& items, bool schnorr) {
    std::size_t ok = 0;
    for (auto const& it : items) {
        secp256k1_pubkey pk;
        if (secp256k1_ec_pubkey_parse(ctx, &pk, it.pubkey.data(), it.pubkey.size()) != 1) continue;
        if (schnorr) {
            ok += secp256k1_schnorr_verify(ctx, it.sig.data(), it.msg.data(), &pk) == 1;
        } else {
            secp256k1_ecdsa_signature sig;
            if (secp256k1_ecdsa_signature_parse_compact(ctx, &sig, it.sig.data()) != 1) continue;
            ok += secp256k1_ecdsa_verify(ctx, &sig, it.msg.data(), &pk) == 1;
        }
    }
    return ok;
}

std::size_t verify_batch(std::vector<signature_input> const& in, std::vector<std::uint8_t>& out, bool schnorr, backend b) {
    return schnorr ? verify_schnorr_bch(in, out, b) : verify_ecdsa(in, out, b);
}

void bench_scheme(bool schnorr, std::size_t n) {
    char const* name = schnorr ? "Schnorr (BCH)" : "ECDSA";
    auto const items = make_items(n, schnorr, schnorr ? 7 : 11);
    std::vector<signature_input> in;
    for (auto const& it : items) in.push_back(it.view());
    std::vector<std::uint8_t> out(n);

    Bench b;
    b.title(std::string(name) + " verify, batch of " + std::to_string(n))
        .unit("signature")
        .batch(n)
        .relative(true)
        .minEpochIterations(2)
        .warmup(1);
    b.run("libsecp256k1, one at a time", [&] {
        ankerl::nanobench::doNotOptimizeAway(verify_one_by_one(items, schnorr));
    });
    b.run("secp256k1-turbo, portable 1-lane", [&] {
        ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, backend::scalar_portable));
    });
    if (backend_available(backend::avx512_ifma)) {
        b.run("secp256k1-turbo, AVX-512 IFMA 8-lane", [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, backend::avx512_ifma));
        });
    }
    for (backend g : {backend::cuda, backend::metal}) {
        if (!backend_available(g)) continue;
        b.run(std::string("secp256k1-turbo, ") + std::string(backend_name(g)), [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, g));
        });
    }
    if (schnorr) {
        b.run("libsecp256k1, algebraic batch", [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_schnorr_bch_all(in, seed));
        });
    }
}

// GPU cost per signature by batch size (end to end: preprocessing, upload,
// kernel, download), next to the best CPU backend, to locate the crossover.
void bench_gpu_sizes(bool schnorr) {
    backend gpu_b = backend::secp256k1_fallback;
    for (backend g : {backend::cuda, backend::metal}) if (backend_available(g)) gpu_b = g;
    if (gpu_b == backend::secp256k1_fallback) return;
    char const* name = schnorr ? "Schnorr (BCH)" : "ECDSA";
    std::size_t const max_n = 65536;
    auto const items = make_items(max_n, schnorr, 41);
    backend const cpu_b = best_backend();
    Bench b;
    b.title(std::string(name) + ": " + std::string(backend_name(gpu_b)) + " vs " + std::string(backend_name(cpu_b)) + ", by batch size")
        .unit("signature").relative(true).minEpochIterations(2);
    for (std::size_t n : {std::size_t(64), std::size_t(256), std::size_t(1024), std::size_t(4096), std::size_t(16384), max_n}) {
        std::vector<signature_input> in;
        for (std::size_t i = 0; i < n; ++i) in.push_back(items[i].view());
        std::vector<std::uint8_t> out(n);
        b.batch(n).run("gpu batch of " + std::to_string(n), [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, gpu_b));
        });
        if (n <= 4096) {
            b.batch(n).run("cpu batch of " + std::to_string(n), [&] {
                ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, cpu_b));
            });
        }
    }
}

// Algebraic batch (random linear combination, one multi-scalar
// multiplication) versus the lane-parallel kernel, by batch size.
void bench_schnorr_algebraic_sizes() {
    auto const items = make_items(2048, true, 31);
    Bench b;
    b.title("Schnorr (BCH): algebraic batch vs AVX-512 IFMA lanes, by batch size").unit("signature").relative(true).minEpochIterations(4);
    for (std::size_t n : {std::size_t(2), std::size_t(8), std::size_t(32), std::size_t(128), std::size_t(512), std::size_t(2048)}) {
        std::vector<signature_input> in;
        for (std::size_t i = 0; i < n; ++i) in.push_back(items[i].view());
        std::vector<std::uint8_t> out(n);
        b.batch(n).run("algebraic batch of " + std::to_string(n), [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_schnorr_bch_all(in, seed));
        });
        if (backend_available(backend::avx512_ifma)) {
            b.batch(n).run("AVX-512 IFMA batch of " + std::to_string(n), [&] {
                ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, true, backend::avx512_ifma));
            });
        }
    }
}

void bench_batch_sizes(bool schnorr) {
    if (!backend_available(backend::avx512_ifma)) return;
    char const* name = schnorr ? "Schnorr (BCH)" : "ECDSA";
    auto const items = make_items(1024, schnorr, 23);
    Bench b;
    b.title(std::string(name) + " AVX-512 IFMA, cost per signature by batch size").unit("signature").relative(true).minEpochIterations(4);
    for (std::size_t n : {std::size_t(1), std::size_t(4), std::size_t(8), std::size_t(16), std::size_t(64), std::size_t(256), std::size_t(1024)}) {
        std::vector<signature_input> in;
        for (std::size_t i = 0; i < n; ++i) in.push_back(items[i].view());
        std::vector<std::uint8_t> out(n);
        b.batch(n).run("batch of " + std::to_string(n), [&] {
            ankerl::nanobench::doNotOptimizeAway(verify_batch(in, out, schnorr, backend::avx512_ifma));
        });
    }
}

} // namespace

int main(int argc, char** argv) {
    ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    bool const gpu_only = argc > 1 && std::string(argv[1]) == "gpu";
    std::printf("best backend: %s (%zu lanes)\n", std::string(backend_name(best_backend())).c_str(), lanes(best_backend()));
    for (backend g : {backend::cuda, backend::metal}) {
        if (backend_available(g)) std::printf("gpu backend: %s on %s\n", std::string(backend_name(g)).c_str(), device_name(g).c_str());
    }
    set_gpu_min_batch(1);
    if (gpu_only) {
        bench_gpu_sizes(false);
        bench_gpu_sizes(true);
        secp256k1_context_destroy(ctx);
        return 0;
    }
    bench_scheme(false, 2048);
    bench_scheme(true, 2048);
    bench_gpu_sizes(false);
    bench_gpu_sizes(true);
    bench_batch_sizes(false);
    bench_batch_sizes(true);
    bench_schnorr_algebraic_sizes();
    secp256k1_context_destroy(ctx);
    return 0;
}
