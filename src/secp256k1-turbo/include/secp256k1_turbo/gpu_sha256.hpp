// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Double SHA-256 on the GPU, for the hashing a block does in bulk: the nodes
// of its merkle tree.
//
// It runs on the device the signature kernels use (CUDA or Metal, whichever
// the build includes and finds at run time), one thread per hash. A launch has
// a fixed cost, so it pays for large batches only: min_batch() is the size from
// which the callers of this library are expected to prefer it.
//
// Every function returns false when the work was not done: the build has no
// GPU runtime, no device was found, or the device failed. Nothing is written
// then, and the caller hashes on the CPU. There is no partial result.

#ifndef KTH_SECP256K1_TURBO_GPU_SHA256_HPP
#define KTH_SECP256K1_TURBO_GPU_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace secp256k1_turbo::gpu_sha256 {

// A device is present and its runtime started.
bool available();
// Its name ("" when unavailable).
std::string device_name();

// The number of hashes from which a GPU batch is worth its launch, 0 when the
// device is not to be used (there is none, or it was turned off). The default
// depends on the device and was measured (see the README); the environment
// variable KTH_SECP256K1_TURBO_GPU_SHA256_MIN_BATCH sets the initial value,
// and "off" or 0, like set_min_batch(0), tells callers never to use the device.
std::size_t min_batch();
void set_min_batch(std::size_t n);
// available() and n >= min_batch().
bool worth_it(std::size_t n);

// Double SHA-256 of `count` 64-byte inputs: out[32 * i .. 32 * i + 31] is the
// hash of in[64 * i .. 64 * i + 63]. `out` must not overlap `in`.
bool sha256d64(std::uint8_t* out, std::uint8_t const* in, std::size_t count);

// Merkle root of `count` >= 1 hashes of 32 bytes, by the Bitcoin rule: the last
// hash of a level with an odd number of them is paired with itself. Every
// level is hashed on the device and only the root comes back.
//
// *mutated is set to whether two equal hashes were paired as siblings at any
// level (not counting the last hash paired with itself): such a list has the
// same root as a shorter one, so the root does not identify it.
bool merkle_root(std::uint8_t* root32, std::uint8_t const* hashes, std::size_t count, bool* mutated = nullptr);

} // namespace secp256k1_turbo::gpu_sha256

#endif
