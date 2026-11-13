// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Host side of the GPU backends: input layout, the shared G table in device
// layout, and the entry points of the CUDA and Metal runtimes. Every runtime
// call returns false on failure (no device, driver error), in which case the
// caller verifies the batch on the CPU instead.

#ifndef KTH_SECP256K1_TURBO_GPU_BACKEND_HPP
#define KTH_SECP256K1_TURBO_GPU_BACKEND_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "kernel_core.h"

namespace secp256k1_turbo {

struct signature_input;

namespace gpu {

// Lays out one signature for the kernel (the same for ECDSA and Schnorr):
// byte-order conversion and the structural checks on the key encoding.
void fill_raw_input(signature_input const& in, gpu_raw_input& out);

// The G comb table as the kernel reads it (GPU_G_WORDS words, limb-major).
std::uint32_t const* g_table_words();

// The kernel compiled for the host (reference and fallback for the tests).
std::uint32_t verify_one_host(gpu_raw_input const& in, bool schnorr);

// The device-side field self-test (tests only).
void field_test_host(gpu_field_test_in const& in, gpu_field_test_out& out);

// The runtimes hand the caller their own input buffer (pinned or shared
// memory) to fill, so a batch is written once: fill(dst, begin, end) must
// lay out entries [begin, end) at dst[begin] .. dst[end - 1].
using fill_fn = std::function<void(gpu_raw_input* dst, std::size_t begin, std::size_t end)>;

#if defined(KTH_SECP256K1_TURBO_HAVE_CUDA)
bool cuda_available();
std::string cuda_device_name();
// Verdicts (0 or 1) to out[0] .. out[n - 1].
bool cuda_verify(std::size_t n, bool schnorr, fill_fn const& fill, std::uint8_t* out);
bool cuda_field_test(gpu_field_test_in const* in, std::size_t n, gpu_field_test_out* out);
#endif

#if defined(KTH_SECP256K1_TURBO_HAVE_METAL)
bool metal_available();
std::string metal_device_name();
bool metal_verify(std::size_t n, bool schnorr, fill_fn const& fill, std::uint8_t* out);
bool metal_field_test(gpu_field_test_in const* in, std::size_t n, gpu_field_test_out* out);
#endif

} // namespace gpu
} // namespace secp256k1_turbo

#endif
