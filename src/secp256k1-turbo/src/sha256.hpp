// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SECP256K1_TURBO_SHA256_HPP
#define KTH_SECP256K1_TURBO_SHA256_HPP

#include <cstddef>
#include <cstdint>

namespace secp256k1_turbo {

// Single-shot SHA-256 (the BCH Schnorr challenge hashes 97 bytes).
void sha256(std::uint8_t const* data, std::size_t size, std::uint8_t out[32]);

} // namespace secp256k1_turbo

#endif
