// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/sip_hash.hpp>

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND                                                               \
    do {                                                                       \
        v0 += v1;                                                              \
        v1 = ROTL(v1, 13);                                                     \
        v1 ^= v0;                                                              \
        v0 = ROTL(v0, 32);                                                     \
        v2 += v3;                                                              \
        v3 = ROTL(v3, 16);                                                     \
        v3 ^= v2;                                                              \
        v0 += v3;                                                              \
        v3 = ROTL(v3, 21);                                                     \
        v3 ^= v0;                                                              \
        v2 += v1;                                                              \
        v1 = ROTL(v1, 17);                                                     \
        v1 ^= v2;                                                              \
        v2 = ROTL(v2, 32);                                                     \
    } while (0)

namespace kth {

sip_hasher::sip_hasher(uint64_t k0, uint64_t k1)
    : v { 0x736f6d6570736575ULL ^ k0
        , 0x646f72616e646f6dULL ^ k1
        , 0x6c7967656e657261ULL ^ k0
        , 0x7465646279746573ULL ^ k1}
    , tmp(0)
    , count(0)
{}

sip_hasher& sip_hasher::write(uint64_t data) {
    uint64_t v0 = v[0],
             v1 = v[1],
             v2 = v[2],
             v3 = v[3];

    //class invariant
    assert(count % 8 == 0);

    v3 ^= data;
    SIPROUND;
    SIPROUND;
    v0 ^= data;

    v[0] = v0;
    v[1] = v1;
    v[2] = v2;
    v[3] = v3;

    count += 8;
    return *this;
}

sip_hasher& sip_hasher::write(uint8_t const* data, size_t size) {
    uint64_t v0 = v[0],
             v1 = v[1],
             v2 = v[2],
             v3 = v[3];

    uint64_t t = tmp;
    int c = count;

    while (size-- != 0u) {
        t |= uint64_t(*(data++)) << (8 * (c % 8));
        c++;
        if ((c & 7) == 0) {
            v3 ^= t;
            SIPROUND;
            SIPROUND;
            v0 ^= t;
            t = 0;
        }
    }

    v[0] = v0;
    v[1] = v1;
    v[2] = v2;
    v[3] = v3;
    count = c;
    tmp = t;

    return *this;
}

uint64_t sip_hasher::finalize() const {
    uint64_t v0 = v[0],
             v1 = v[1],
             v2 = v[2],
             v3 = v[3];

    uint64_t t = tmp | (uint64_t(count) << 56);

    v3 ^= t;
    SIPROUND;
    SIPROUND;
    v0 ^= t;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t sip_hash_uint256(uint64_t k0, uint64_t k1, hash_digest const& val) {
    /* Specialized implementation for efficiency */
    uint64_t d = get_uint64<0>(val);

    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1 ^ d;

    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<1>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<2>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<3>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    v3 ^= uint64_t(4) << 59;
    SIPROUND;
    SIPROUND;
    v0 ^= uint64_t(4) << 59;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t sip_hash_uint256_extra(uint64_t k0, uint64_t k1, hash_digest const& val, uint32_t extra) {
    /* Specialized implementation for efficiency */
    uint64_t d = get_uint64<0>(val);

    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1 ^ d;

    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<1>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<2>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = get_uint64<3>(val);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = (uint64_t(36) << 56) | extra;
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

} // namespace kth
