// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <secp256k1_turbo/batch_verify.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include "cpu.hpp"
#include "g_table.hpp"
#include "kernel.hpp"
#include "prepare.hpp"
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA) || defined(KTH_SECP256K1_TURBO_HAVE_METAL)
#include "../gpu/gpu_backend.hpp"
#endif

namespace secp256k1_turbo {

namespace {

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
#if defined(KTH_SECP256K1_TURBO_HAVE_AVX512IFMA)
        case backend::avx512_ifma: return &kernel_avx512ifma;
#endif
        default: return nullptr;
    }
}

// A kernel run costs about the same with 1 or 8 lanes busy (measured on Zen 5:
// one 8-lane run ~ 2.5 libsecp256k1 verifications, ECDSA and Schnorr alike).
// So a batch with no more than this many signatures in its last, partial group
// verifies that group one at a time instead: a batch of 1 or 2 never reaches
// the lanes, and 9 or 10 signatures take one run plus one or two verifications.
constexpr std::size_t max_scalar_tail = 2;

constexpr std::size_t gpu_disabled = std::numeric_limits<std::size_t>::max();

// KTH_SECP256K1_TURBO_GPU_MIN_BATCH overrides the default threshold; "off" or
// 0 keeps every batch on the CPU (the device stays available for explicit
// requests).
std::size_t initial_gpu_min_batch() {
    char const* s = std::getenv("KTH_SECP256K1_TURBO_GPU_MIN_BATCH");
    if (s == nullptr || *s == '\0') return 512;
    if (std::strcmp(s, "off") == 0) return gpu_disabled;
    char* end = nullptr;
    unsigned long long const v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0' || v == 0) return gpu_disabled;
    return static_cast<std::size_t>(v);
}

std::atomic<std::size_t> gpu_min_batch_value{initial_gpu_min_batch()};
std::atomic<std::size_t> host_threads_value{0};  // 0: automatic

bool is_gpu(backend b) { return b == backend::cuda || b == backend::metal; }

// Runs fn(begin, end) over [0, n) in chunks on up to hardware_concurrency()
// threads (the caller's included). Below min_chunk items it runs inline.
template <class Fn>
void parallel_chunks(std::size_t n, std::size_t min_chunk, Fn const& fn) {
    // Spawning a thread costs tens of microseconds: a handful of big chunks
    // beats one per hardware thread for the batch sizes a GPU is used for.
    std::size_t hw = host_threads_value.load(std::memory_order_relaxed);
    if (hw == 0) hw = std::min<std::size_t>(8, std::max<std::size_t>(1, std::thread::hardware_concurrency() / 2));
    std::size_t const threads = std::min(hw, std::max<std::size_t>(1, n / min_chunk));
    if (threads <= 1) { fn(0, n); return; }
    std::size_t const chunk = (n + threads - 1) / threads;
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (std::size_t t = 1; t < threads; ++t) {
        std::size_t const b = t * chunk, e = std::min(n, b + chunk);
        if (b >= e) break;
        pool.emplace_back([&fn, b, e] { fn(b, e); });
    }
    fn(0, std::min(n, chunk));
    for (auto& th : pool) th.join();
}

// Host preprocessing of a range: parse, scalars, and (ECDSA) the batched
// inversion over that range only, so ranges are independent.
void prepare_range(std::span<signature_input const> inputs, std::vector<prepared>& items, check_mode mode, std::size_t b, std::size_t e) {
    for (std::size_t i = b; i < e; ++i) {
        if (mode == check_mode::ecdsa) prepare_ecdsa(inputs[i], items[i]);
        else prepare_schnorr(inputs[i], items[i]);
    }
    if (mode == check_mode::ecdsa) finish_ecdsa_scalars(items, b, e);
}

// KTH_SECP256K1_TURBO_TRACE=1 prints the host/device split of every GPU batch.
bool trace_enabled() {
    static bool const on = std::getenv("KTH_SECP256K1_TURBO_TRACE") != nullptr;
    return on;
}
using clock_t_ = std::chrono::steady_clock;
double us_since(clock_t_::time_point t0) {
    return std::chrono::duration<double, std::micro>(clock_t_::now() - t0).count();
}

#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA) || defined(KTH_SECP256K1_TURBO_HAVE_METAL)
// A GPU chunk large enough to spread its layout over host threads.
constexpr std::size_t gpu_parallel_chunk = 1024;

// Returns false when the device could not run the batch (the caller then
// verifies on the CPU).
bool run_gpu(std::span<signature_input const> inputs, std::span<std::uint8_t> results, check_mode mode, backend b, std::size_t& valid) {
    auto const t0 = clock_t_::now();
    bool const schnorr = mode == check_mode::schnorr_bch;
    double fill_us = 0;
    // The batch is laid out straight into the runtime's input buffer, one
    // chunk at a time while the device works on the previous ones.
    gpu::fill_fn const fill = [&](gpu_raw_input* dst, std::size_t begin, std::size_t end) {
        auto const t1 = clock_t_::now();
        parallel_chunks(end - begin, gpu_parallel_chunk, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t i = begin + lo; i < begin + hi; ++i) gpu::fill_raw_input(inputs[i], dst[i]);
        });
        fill_us += us_since(t1);
    };
    bool ok = false;
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (b == backend::cuda) ok = gpu::cuda_verify(inputs.size(), schnorr, fill, results.data());
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (b == backend::metal) ok = gpu::metal_verify(inputs.size(), schnorr, fill, results.data());
#endif
    if (trace_enabled()) {
        double const total_us = us_since(t0);
        std::fprintf(stderr, "secp256k1-turbo gpu: n=%zu host layout=%.0fus total=%.0fus (%.0f ns/sig)\n",
                     inputs.size(), fill_us, total_us, 1000.0 * total_us / inputs.size());
    }
    if (!ok) return false;
    valid = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) valid += results[i];
    return true;
}
#endif

std::size_t verify(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b, check_mode mode) {
    if (!backend_available(b)) b = best_backend();
    if (is_gpu(b) && inputs.size() < gpu_min_batch_value.load(std::memory_order_relaxed)) b = best_backend();
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA) || defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (is_gpu(b)) {
        // The device path needs no host-side preparation: no per-item
        // state is allocated (at a million signatures that alone would be
        // hundreds of megabytes to zero and fault in on every call).
        std::size_t valid = 0;
        if (run_gpu(inputs, results, mode, b, valid)) return valid;
        // The device failed (driver, memory): verify on the CPU instead.
        b = best_backend();
    }
#endif
    kernel_fn const kernel = kernel_for(b);
    if (kernel == nullptr) return run_fallback(inputs, results, mode);
    std::size_t lane_count = inputs.size();
    if (b == backend::avx512_ifma) {
        std::size_t const tail = inputs.size() % max_lanes;
        if (tail != 0 && tail <= max_scalar_tail) lane_count -= tail;
    }
    std::size_t const valid = run_fallback(inputs.subspan(lane_count), results.subspan(lane_count), mode);
    if (lane_count == 0) return valid;
    std::vector<prepared> items(lane_count);
    prepare_range(inputs, items, mode, 0, lane_count);
    return valid + run_lanes(items, results, mode, kernel);
}

} // namespace

bool backend_available(backend b) {
    switch (b) {
        case backend::secp256k1_fallback: return true;
        case backend::scalar_portable: return true;
        case backend::avx512_ifma:
#if defined(KTH_SECP256K1_TURBO_HAVE_AVX512IFMA) && defined(__AVX512IFMA__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__AVX512BW__)
            // The whole build targets an IFMA microarchitecture: static choice, no CPUID.
            return true;
#elif defined(KTH_SECP256K1_TURBO_HAVE_AVX512IFMA)
            { static bool const ok = cpu_supports_avx512ifma(); return ok; }
#else
            return false;
#endif
        case backend::cuda:
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
            return gpu::cuda_available();
#else
            return false;
#endif
        case backend::metal:
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
            return gpu::metal_available();
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

backend best_backend(std::size_t n) {
    std::size_t const min = gpu_min_batch_value.load(std::memory_order_relaxed);
    if (min != gpu_disabled && n >= min) {
        if (backend_available(backend::cuda)) return backend::cuda;
        if (backend_available(backend::metal)) return backend::metal;
    }
    return best_backend();
}

std::size_t gpu_min_batch() { return gpu_min_batch_value.load(std::memory_order_relaxed); }
void set_gpu_min_batch(std::size_t n) { gpu_min_batch_value.store(n == 0 ? gpu_disabled : n, std::memory_order_relaxed); }
std::size_t host_threads() { return host_threads_value.load(std::memory_order_relaxed); }
void set_host_threads(std::size_t n) { host_threads_value.store(n, std::memory_order_relaxed); }

std::string device_name(backend b) {
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (b == backend::cuda) return gpu::cuda_device_name();
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (b == backend::metal) return gpu::metal_device_name();
#endif
    (void)b;
    return "";
}

std::string_view backend_name(backend b) {
    switch (b) {
        case backend::secp256k1_fallback: return "secp256k1 (scalar, per signature)";
        case backend::scalar_portable: return "portable 1-lane";
        case backend::avx512_ifma: return "avx512-ifma 8-lane";
        case backend::cuda: return "cuda (one thread per signature)";
        case backend::metal: return "metal (one thread per signature)";
    }
    return "?";
}

std::size_t lanes(backend b) {
    if (is_gpu(b)) return 0;
    return b == backend::avx512_ifma ? 8 : 1;
}

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

} // namespace secp256k1_turbo
