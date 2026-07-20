// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SIP_HASH_HPP_
#define KTH_SIP_HASH_HPP_

#include <cstdint>

#include <kth/infrastructure/math/hash.hpp>
//#include <kth/infrastructure/math/uint256.hpp>

namespace kth {

//using uint256_t = boost::multiprecision::uint256_t;
//
//template <size_t i>
//uint64_t get_uint64(uint256_t const& x) {
//    auto const* ptr = reinterpret_cast<uint8_t const*>(x.backend().limbs()) + i * sizeof(uint64_t);
//
//    return ((uint64_t)ptr[0]) | ((uint64_t)ptr[1]) << 8 |
//            ((uint64_t)ptr[2]) << 16 | ((uint64_t)ptr[3]) << 24 |
//            ((uint64_t)ptr[4]) << 32 | ((uint64_t)ptr[5]) << 40 |
//            ((uint64_t)ptr[6]) << 48 | ((uint64_t)ptr[7]) << 56;
//}


template <size_t i>
uint64_t get_uint64(hash_digest const& x) {
    //precondition: 0 <= i < sizeof(hash_digest) / sizeof(uint64_t)

    auto const* ptr = x.data() + i * sizeof(uint64_t);

    return ((uint64_t)ptr[0]) | ((uint64_t)ptr[1]) << 8 |
            ((uint64_t)ptr[2]) << 16 | ((uint64_t)ptr[3]) << 24 |
            ((uint64_t)ptr[4]) << 32 | ((uint64_t)ptr[5]) << 40 |
            ((uint64_t)ptr[6]) << 48 | ((uint64_t)ptr[7]) << 56;
}

/** SipHash-2-4 */
class sip_hasher {
private:
    uint64_t v[4];
    uint64_t tmp;
    int count;

public:
    /** Construct a SipHash calculator initialized with 128-bit key (k0, k1) */
    sip_hasher(uint64_t k0, uint64_t k1);

    /**
     * Hash a 64-bit integer worth of data.
     * It is treated as if this was the little-endian interpretation of 8 bytes.
     * This function can only be used when a multiple of 8 bytes have been
     * written so far.
     */
    sip_hasher& write(uint64_t data);

    /** Hash arbitrary bytes. */
    sip_hasher& write(uint8_t const* data, size_t size);

    /** Compute the 64-bit SipHash-2-4 of the data written so far. The object
     * remains untouched. */
    uint64_t finalize() const;
};

/** Optimized SipHash-2-4 implementation for uint256.
 *
 *  It is identical to:
 *    sip_hasher(k0, k1)
 *      .write(val.GetUint64(0))
 *      .write(val.GetUint64(1))
 *      .write(val.GetUint64(2))
 *      .write(val.GetUint64(3))
 *      .finalize()
 */
//uint64_t sip_hash_uint256(uint64_t k0, uint64_t k1, uint256_t const& val);
//uint64_t sip_hash_uint256_extra(uint64_t k0, uint64_t k1, uint256_t const& val, uint32_t extra);

uint64_t sip_hash_uint256(uint64_t k0, uint64_t k1, hash_digest const& val);
uint64_t sip_hash_uint256_extra(uint64_t k0, uint64_t k1, hash_digest const& val, uint32_t extra);

} // namespace kth

#endif /* KTH_SIP_HASH_HPP_ */
