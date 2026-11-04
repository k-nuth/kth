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
// signature at a time, so the API is always usable.

#ifndef KTH_SECP256K1_SIMD_BATCH_VERIFY_HPP
#define KTH_SECP256K1_SIMD_BATCH_VERIFY_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace kth::secp256k1_simd {

enum class backend {
    secp256k1_fallback,  // libsecp256k1, one signature at a time
    scalar_portable,     // this library's algorithm, one lane (reference/testing)
    avx512_ifma          // 8 lanes
};

struct signature_input {
    std::uint8_t const* sig64;     // ECDSA: r || s big-endian (compact); Schnorr: BCH 64-byte signature
    std::uint8_t const* msg32;     // message hash
    std::uint8_t const* pubkey;    // 33 (02/03) or 65 (04/06/07) bytes
    std::size_t pubkey_size;
};

// Fastest backend supported by the running CPU.
backend best_backend();
bool backend_available(backend b);
std::string_view backend_name(backend b);
std::size_t lanes(backend b);

// results[i] = 1 if inputs[i] is a valid signature, 0 otherwise. Returns the
// number of valid signatures. results.size() must be >= inputs.size().
// Batches of fewer than two signatures go through libsecp256k1 even when a
// vector backend is selected (a mostly idle 8-lane run costs more).
std::size_t verify_ecdsa(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b = best_backend());
std::size_t verify_schnorr_bch(std::span<signature_input const> inputs, std::span<std::uint8_t> results, backend b = best_backend());

// Algebraic batch verification of BCH Schnorr signatures
// (secp256k1_schnorr_verify_batch): one random linear combination checked
// with a single multi-scalar multiplication. Returns true only when every
// signature is valid; there is no per-signature verdict. seed32 is optional
// fresh randomness for the randomizers.
bool verify_schnorr_bch_all(std::span<signature_input const> inputs, std::uint8_t const* seed32 = nullptr);

} // namespace kth::secp256k1_simd

#endif
