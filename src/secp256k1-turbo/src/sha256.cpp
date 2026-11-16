// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sha256.hpp"

#include <cstring>

namespace secp256k1_turbo {

namespace {

constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void transform(std::uint32_t s[8], std::uint8_t const* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[4 * i]) << 24) | (std::uint32_t(block[4 * i + 1]) << 16) |
               (std::uint32_t(block[4 * i + 2]) << 8) | std::uint32_t(block[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5], g = s[6], h = s[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t const ch = (e & f) ^ (~e & g);
        std::uint32_t const t1 = h + S1 + ch + K[i] + w[i];
        std::uint32_t const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t const t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d; s[4] += e; s[5] += f; s[6] += g; s[7] += h;
}

} // namespace

void sha256(std::uint8_t const* data, std::size_t size, std::uint8_t out[32]) {
    std::uint32_t s[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t i = 0;
    for (; i + 64 <= size; i += 64) transform(s, data + i);
    std::uint8_t tail[128];
    std::size_t const rem = size - i;
    std::memcpy(tail, data + i, rem);
    tail[rem] = 0x80;
    std::size_t const total = (rem + 1 + 8 <= 64) ? 64 : 128;
    std::memset(tail + rem + 1, 0, total - rem - 1 - 8);
    std::uint64_t const bits = static_cast<std::uint64_t>(size) * 8;
    for (int j = 0; j < 8; ++j) tail[total - 1 - j] = static_cast<std::uint8_t>(bits >> (8 * j));
    transform(s, tail);
    if (total == 128) transform(s, tail + 64);
    for (int j = 0; j < 8; ++j) {
        out[4 * j] = static_cast<std::uint8_t>(s[j] >> 24);
        out[4 * j + 1] = static_cast<std::uint8_t>(s[j] >> 16);
        out[4 * j + 2] = static_cast<std::uint8_t>(s[j] >> 8);
        out[4 * j + 3] = static_cast<std::uint8_t>(s[j]);
    }
}

} // namespace secp256k1_turbo
