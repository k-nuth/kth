// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <secp256k1_turbo/gpu_sha256.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA) || defined(KTH_SECP256K1_TURBO_HAVE_METAL)
#include "../gpu/gpu_backend.hpp"
#endif

namespace secp256k1_turbo::gpu_sha256 {

namespace {

constexpr std::size_t disabled = std::numeric_limits<std::size_t>::max();
// Not set: the default of the device, which is only known once it is looked for.
constexpr std::size_t by_device = disabled - 1;

// From this many hashes a merkle tree is clearly cheaper on the device than on
// one core with SHA extensions (see the README of this library for the
// measurements). A discrete GPU starts a launch in tens of microseconds; on
// Apple GPUs a command buffer takes about half a millisecond.
[[maybe_unused]] constexpr std::size_t cuda_min_batch = 2048;
[[maybe_unused]] constexpr std::size_t metal_min_batch = 32768;

std::size_t initial_min_batch() {
    char const* s = std::getenv("KTH_SECP256K1_TURBO_GPU_SHA256_MIN_BATCH");
    if (s == nullptr || *s == '\0') return by_device;
    if (std::strcmp(s, "off") == 0) return disabled;
    char* end = nullptr;
    unsigned long long const v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0' || v == 0) return disabled;
    return static_cast<std::size_t>(v);
}

std::atomic<std::size_t> min_batch_value{initial_min_batch()};

// The threshold in force: the one that was set, or the default of the device
// that is present.
std::size_t threshold() {
    std::size_t const n = min_batch_value.load(std::memory_order_relaxed);
    if (n != by_device) return n;
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (gpu::cuda_available()) return cuda_min_batch;
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (gpu::metal_available()) return metal_min_batch;
#endif
    return disabled;
}

} // namespace

bool available() {
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (gpu::cuda_available()) return true;
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (gpu::metal_available()) return true;
#endif
    return false;
}

std::string device_name() {
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (gpu::cuda_available()) return gpu::cuda_device_name();
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (gpu::metal_available()) return gpu::metal_device_name();
#endif
    return {};
}

std::size_t min_batch() {
    std::size_t const n = threshold();
    return n == disabled ? 0 : n;
}

void set_min_batch(std::size_t n) {
    min_batch_value.store(n == 0 ? disabled : n, std::memory_order_relaxed);
}

bool worth_it(std::size_t n) {
    return n >= threshold() && available();
}

bool sha256d64(std::uint8_t* out, std::uint8_t const* in, std::size_t count) {
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (gpu::cuda_available()) return gpu::cuda_sha256d64(in, count, out);
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (gpu::metal_available()) return gpu::metal_sha256d64(in, count, out);
#endif
    (void)out; (void)in; (void)count;
    return false;
}

bool merkle_root(std::uint8_t* root32, std::uint8_t const* hashes, std::size_t count, bool* mutated) {
    if (count == 0) return false;
#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
    if (gpu::cuda_available()) return gpu::cuda_merkle_root(hashes, count, root32, mutated);
#endif
#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
    if (gpu::metal_available()) return gpu::metal_merkle_root(hashes, count, root32, mutated);
#endif
    (void)root32; (void)hashes; (void)mutated;
    return false;
}

} // namespace secp256k1_turbo::gpu_sha256
