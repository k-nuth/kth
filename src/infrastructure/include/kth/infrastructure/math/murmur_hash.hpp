// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MATH_MURMUR_HASH_HPP
#define KTH_INFRASTRUCTURE_MATH_MURMUR_HASH_HPP

#include <bit>
#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// MurmurHash3, the 32-bit variant for x86. Not a cryptographic hash: anyone
// can make two inputs collide. BIP37 picks the bits of a peer's bloom filter
// with it.
[[nodiscard]]
constexpr uint32_t murmur_hash3(uint32_t seed, byte_span data) noexcept {
    constexpr uint32_t c1 = 0xcc9e2d51;
    constexpr uint32_t c2 = 0x1b873593;

    auto hash = seed;
    auto const blocks = data.size() / 4;
    for (size_t i = 0; i < blocks; ++i) {
        auto k = uint32_t(data[i * 4]) | (uint32_t(data[i * 4 + 1]) << 8)
            | (uint32_t(data[i * 4 + 2]) << 16) | (uint32_t(data[i * 4 + 3]) << 24);
        k *= c1;
        k = std::rotl(k, 15);
        k *= c2;
        hash ^= k;
        hash = std::rotl(hash, 13);
        hash = hash * 5 + 0xe6546b64;
    }

    // The one to three bytes after the last whole word.
    auto const tail = data.subspan(blocks * 4);
    uint32_t k = 0;
    if (tail.size() >= 3) k ^= uint32_t(tail[2]) << 16;
    if (tail.size() >= 2) k ^= uint32_t(tail[1]) << 8;
    if (tail.size() >= 1) {
        k ^= uint32_t(tail[0]);
        k *= c1;
        k = std::rotl(k, 15);
        k *= c2;
        hash ^= k;
    }

    hash ^= uint32_t(data.size());
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    return hash;
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_MATH_MURMUR_HASH_HPP
