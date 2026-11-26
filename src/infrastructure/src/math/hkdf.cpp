// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/hkdf.hpp>

#include <algorithm>
#include <cstdint>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth {

namespace {

// hash_size is kth's: the 32 bytes of a SHA-256 digest.
constexpr size_t max_output = 255 * hash_size;

// An empty span may hold a null pointer, and the HMAC hands its pointer to
// memcpy even for zero bytes, which is undefined for a null pointer. Noise's
// Split extracts from an empty input key, so an empty one points here.
byte_span not_null(byte_span bytes) {
    static constexpr uint8_t nothing = 0;
    return bytes.empty() ? byte_span{&nothing, 0} : bytes;
}

} // namespace

hash_digest hkdf_sha256_extract(byte_span salt, byte_span input_key) {
    // hmac_sha256_hash takes the data first and the key second.
    if (salt.empty()) {
        hash_digest const zero_salt{};
        return hmac_sha256_hash(not_null(input_key), zero_salt);
    }
    return hmac_sha256_hash(not_null(input_key), salt);
}

std::optional<data_chunk> hkdf_sha256_expand(hash_digest const& prk, byte_span info,
    size_t length) {
    if (length > max_output) {
        return std::nullopt;
    }

    // T(i) = HMAC(prk, T(i-1) || info || i), with T(0) empty. Every T(i) and
    // the input that holds the one before it are keying material, wiped once
    // the output is complete; the last input is the longest of them.
    data_chunk output;
    output.reserve(length);
    data_chunk block;
    block.reserve(hash_size + info.size() + 1);
    hash_digest previous{};
    for (uint8_t counter = 1; output.size() < length; ++counter) {
        block.clear();
        if (counter > 1) {
            block.insert(block.end(), previous.begin(), previous.end());
        }
        block.insert(block.end(), info.begin(), info.end());
        block.push_back(counter);
        previous = hmac_sha256_hash(block, prk);
        auto const take = std::min(hash_size, length - output.size());
        output.insert(output.end(), previous.begin(), previous.begin() + take);
    }
    pseudo_random::wipe(block);
    pseudo_random::wipe(previous);
    return output;
}

std::pair<hash_digest, hash_digest> hkdf_sha256_pair(hash_digest const& chaining_key,
    byte_span input_key) {
    auto prk = hkdf_sha256_extract(chaining_key, input_key);
    auto keys = *hkdf_sha256_expand(prk, {}, 2 * hash_size);
    std::pair<hash_digest, hash_digest> result;
    std::copy_n(keys.begin(), hash_size, result.first.begin());
    std::copy_n(keys.begin() + hash_size, hash_size, result.second.begin());
    pseudo_random::wipe(prk);
    pseudo_random::wipe(keys);
    return result;
}

} // namespace kth
