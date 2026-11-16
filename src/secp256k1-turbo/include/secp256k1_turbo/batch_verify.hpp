// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Lane-parallel (SIMD) batch verification of secp256k1 signatures.
//
// Each signature is verified independently, one per SIMD lane, so the result
// is a per-signature verdict with exactly the semantics of
// secp256k1_ecdsa_verify (low-S required, pubkey parsed like
// secp256k1_ec_pubkey_parse) and secp256k1_schnorr_verify (the BCH scheme).
// On CPUs without AVX-512 IFMA the calls fall back to libsecp256k1 one
// signature at a time, so the API is always usable. When the build includes
// a GPU runtime (CUDA or Metal) and a device is present, large batches can
// run there: one thread per signature, same algorithm, same verdicts.

#ifndef KTH_SECP256K1_TURBO_BATCH_VERIFY_HPP
#define KTH_SECP256K1_TURBO_BATCH_VERIFY_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace secp256k1_turbo {

enum class backend {
    secp256k1_fallback,  // libsecp256k1, one signature at a time
    scalar_portable,     // this library's algorithm, one lane (reference/testing)
    avx512_ifma,         // 8 lanes
    cuda,                // NVIDIA GPU, one thread per signature
    metal                // Apple GPU, one thread per signature
};

struct signature_input {
    std::uint8_t const* sig64;     // ECDSA: r || s big-endian (compact); Schnorr: BCH 64-byte signature
    std::uint8_t const* msg32;     // message hash
    std::uint8_t const* pubkey;    // 33 (02/03) or 65 (04/06/07) bytes
    std::size_t pubkey_size;
};

// Fastest backend supported by the running CPU (never a GPU).
backend best_backend();
// Fastest backend for a batch of n signatures: a GPU backend when one is
// available and n >= gpu_min_batch(), otherwise best_backend().
backend best_backend(std::size_t n);
bool backend_available(backend b);
std::string_view backend_name(backend b);
// Device name of a GPU backend ("" when unavailable).
std::string device_name(backend b);
// Signatures a kernel run verifies at once (0 for a GPU: the whole batch).
std::size_t lanes(backend b);

// Below this batch size a GPU backend is not used: verify() routes the batch
// to best_backend() instead, because the fixed cost of a launch exceeds the
// gain. The default is calibrated for a discrete GPU next to a fast CPU; the
// environment variable KTH_SECP256K1_TURBO_GPU_MIN_BATCH sets the initial
// value ("off" or 0, like set_gpu_min_batch(0), keeps every batch on the CPU).
std::size_t gpu_min_batch();
void set_gpu_min_batch(std::size_t n);

// Host threads used to lay out each chunk of a GPU batch for the device.
// 0 (the default) picks a few from the hardware; a caller that already runs
// one batch per core sets 1. The layout overlaps the device work on the
// previous chunks, so one thread keeps up with the device.
std::size_t host_threads();
void set_host_threads(std::size_t n);

// results[i] = 1 if inputs[i] is a valid signature, 0 otherwise. Returns the
// number of valid signatures. results.size() must be >= inputs.size().
// With a vector backend, a last group of one or two signatures (a batch of 1,
// 2, 9, 10, ...) goes through libsecp256k1: a mostly idle 8-lane run costs
// more than verifying them one at a time.
std::size_t verify_ecdsa(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b = best_backend());
std::size_t verify_schnorr_bch(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b = best_backend());

// Algebraic batch verification of BCH Schnorr signatures
// (secp256k1_schnorr_verify_batch): one random linear combination checked
// with a single multi-scalar multiplication. Returns true only when every
// signature is valid; there is no per-signature verdict. seed32 is optional
// fresh randomness for the randomizers.
bool verify_schnorr_bch_all(std::span<signature_input const> inputs, std::uint8_t const* seed32 = nullptr);

} // namespace secp256k1_turbo

#endif
