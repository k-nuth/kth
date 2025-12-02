// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_HASH_HPP
#define KTH_INFRASTUCTURE_HASH_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <boost/functional/hash_fwd.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

// Alias for boost big integer type.
using uint256_t = boost::multiprecision::uint256_t;


constexpr half_hash null_half_hash {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

constexpr quarter_hash null_quarter_hash {{0, 0, 0, 0, 0, 0, 0, 0}};

constexpr long_hash null_long_hash {{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}};

constexpr short_hash null_short_hash {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};


constexpr mini_hash null_mini_hash {{0, 0, 0, 0, 0, 0}};


inline
uint256_t to_uint256(hash_digest const& hash) {
    uint256_t result;
    import_bits(result, hash.begin(), hash.end(), 8, false);
    return result;
}

/// Generate a scrypt hash to fill a byte array.
template <size_t Size>
byte_array<Size> scrypt(byte_span data, byte_span salt, uint64_t N, uint32_t p, uint32_t r);

/// Generate a scrypt hash of specified length.
KI_API data_chunk scrypt(byte_span data, byte_span salt, uint64_t N, uint32_t p, uint32_t r, size_t length);

/// Generate a bitcoin hash.
KI_API hash_digest bitcoin_hash(byte_span data);

//TODO(fernando): see what to do with Currency
#if defined(KTH_CURRENCY_LTC)
/// Generate a litecoin hash.
KI_API hash_digest litecoin_hash(byte_span data);
#endif

/// Generate a bitcoin short hash.
KI_API short_hash bitcoin_short_hash(byte_span data);

/// Generate a ripemd160 hash
KI_API short_hash ripemd160_hash(byte_span data);
KI_API data_chunk ripemd160_hash_chunk(byte_span data);

/// Generate a sha1 hash.
KI_API short_hash sha1_hash(byte_span data);
KI_API data_chunk sha1_hash_chunk(byte_span data);

/// Generate a sha256 hash.
KI_API hash_digest sha256_hash(byte_span data);
KI_API data_chunk sha256_hash_chunk(byte_span data);

/// Generate a sha256 hash.
/// This hash function was used in electrum seed stretching (obsoleted).
KI_API hash_digest sha256_hash(byte_span first, byte_span second);

// Generate a hmac sha256 hash.
KI_API hash_digest hmac_sha256_hash(byte_span data, byte_span key);

/// Generate a sha512 hash.
KI_API long_hash sha512_hash(byte_span data);

/// Generate a hmac sha512 hash.
KI_API long_hash hmac_sha512_hash(byte_span data, byte_span key);

/// Generate a pkcs5 pbkdf2 hmac sha512 hash.
KI_API long_hash pkcs5_pbkdf2_hmac_sha512(byte_span passphrase, byte_span salt, size_t iterations);

} // namespace kth

// Extend std and boost namespaces with our hash wrappers.
//-----------------------------------------------------------------------------

namespace std {
template <size_t Size>
struct hash<kth::byte_array<Size>> {
    size_t operator()(const kth::byte_array<Size>& hash) const {
        return boost::hash_range(hash.begin(), hash.end());
    }
};

template <>
struct hash<kth::byte_span> {
    size_t operator()(kth::byte_span bytes) const {
        return boost::hash_range(bytes.begin(), bytes.end());
    }
};
} // namespace std

namespace boost {
template <size_t Size>
struct hash<kth::byte_array<Size>> {
    size_t operator()(const kth::byte_array<Size>& hash) const {
        return boost::hash_range(hash.begin(), hash.end());
    }
};

template <>
struct hash<kth::byte_span> {
    size_t operator()(kth::byte_span bytes) const {
        return boost::hash_range(bytes.begin(), bytes.end());
    }
};
} // namespace boost

#include <kth/infrastructure/impl/math/hash.ipp>

#endif
