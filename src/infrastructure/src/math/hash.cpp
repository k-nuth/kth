// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/hash.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <new>
#include <stdexcept>

#include "../math/external/crypto_scrypt.h"
#include "../math/external/hmac_sha256.h"
#include "../math/external/hmac_sha512.h"
#include "../math/external/pkcs5_pbkdf2.h"
#include "../math/external/ripemd160.h"
#include "../math/external/sha1.h"
#include "../math/external/sha256.h"
#include "../math/external/sha512.h"

//TODO(fernando): see what to do with Currency
// #ifdef KTH_CURRENCY_LTC
// #include "../math/external/scrypt.h"
// #endif //KTH_CURRENCY_LTC

namespace kth {

hash_digest bitcoin_hash(byte_span data) {
    return sha256_hash(sha256_hash(data));
}

// #ifdef KTH_CURRENCY_LTC
// hash_digest litecoin_hash(byte_span data) {
//     hash_digest hash;
//     scrypt_1024_1_1_256(reinterpret_cast<char const*>(data.data()),
//                         reinterpret_cast<char*>(hash.data()));
//     return hash;
// }
// #endif //KTH_CURRENCY_LTC

short_hash bitcoin_short_hash(byte_span data) {
    return ripemd160_hash(sha256_hash(data));
}

short_hash ripemd160_hash(byte_span data) {
    short_hash hash;
    RMD160(data.data(), data.size(), hash.data());
    return hash;
}

data_chunk ripemd160_hash_chunk(byte_span data) {
    data_chunk hash(short_hash_size);
    RMD160(data.data(), data.size(), hash.data());
    return hash;
}

short_hash sha1_hash(byte_span data) {
    short_hash hash;
    SHA1_(data.data(), data.size(), hash.data());
    return hash;
}

data_chunk sha1_hash_chunk(byte_span data) {
    data_chunk hash(short_hash_size);
    SHA1_(data.data(), data.size(), hash.data());
    return hash;
}

hash_digest sha256_hash(byte_span data) {
    hash_digest hash;
    SHA256_(data.data(), data.size(), hash.data());
    return hash;
}

data_chunk sha256_hash_chunk(byte_span data) {
    data_chunk hash(hash_size);
    SHA256_(data.data(), data.size(), hash.data());
    return hash;
}

hash_digest sha256_hash(byte_span first, byte_span second) {
    hash_digest hash;
    SHA256CTX context;
    SHA256Init(&context);
    SHA256Update(&context, first.data(), first.size());
    SHA256Update(&context, second.data(), second.size());
    SHA256Final(&context, hash.data());
    return hash;
}

hash_digest hmac_sha256_hash(byte_span data, byte_span key) {
    hash_digest hash;
    HMACSHA256(data.data(), data.size(), key.data(), key.size(), hash.data());
    return hash;
}

long_hash sha512_hash(byte_span data) {
    long_hash hash;
    SHA512_(data.data(), data.size(), hash.data());
    return hash;
}

long_hash hmac_sha512_hash(byte_span data, byte_span key) {
    long_hash hash;
    HMACSHA512(data.data(), data.size(), key.data(), key.size(), hash.data());
    return hash;
}

long_hash pkcs5_pbkdf2_hmac_sha512(byte_span passphrase, byte_span salt, size_t iterations) {
    long_hash hash;
    auto const result = pkcs5_pbkdf2(passphrase.data(), passphrase.size(),
        salt.data(), salt.size(), hash.data(), hash.size(), iterations);

    if (result != 0) {
        throw std::bad_alloc();
    }

    return hash;
}

static
void handle_script_result(int result) {
    if (result == 0) {
        return;
    }

    switch (errno) {
        case EFBIG:
            throw std::length_error("scrypt parameter too large");
        case EINVAL:
            throw std::runtime_error("scrypt invalid argument");
        case ENOMEM:
            throw std::length_error("scrypt address space");
        default:
            throw std::bad_alloc();
    }
}

data_chunk scrypt(byte_span data, byte_span salt, uint64_t N, uint32_t p, uint32_t r, size_t length) {
    data_chunk output(length);
    auto const result = crypto_scrypt(data.data(), data.size(), salt.data(),
        salt.size(), N, r, p, output.data(), output.size());
    handle_script_result(result);
    return output;
}

} // namespace kth
