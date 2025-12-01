// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/math/stealth.hpp>

#include <algorithm>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <utility>

namespace kth::domain {

using namespace kth::domain::chain;
using namespace kth::domain::machine;
using namespace kth::infrastructure::machine;

bool is_stealth_script(script const& script) {
    if (script.pattern() != script_pattern::null_data) {
        return false;
    }

    KTH_ASSERT(script.size() == 2);
    return (script[1].data().size() >= hash_size);
}

bool to_stealth_prefix(uint32_t& out_prefix, script const& script) {
    if ( ! is_stealth_script(script)) {
        return false;
    }

    // A stealth prefix is the full 32 bits (prefix to the hash).
    // A stealth filter is a leftmost substring of the stealth prefix.
    ////constexpr size_t size = binary::bits_per_block * sizeof(uint32_t);

    auto const script_hash = bitcoin_hash(script.to_data(false));
    out_prefix = from_little_endian_unsafe<uint32_t>(script_hash);
    return true;
}

// TODO(legacy): this can be implemented using libsecp256k1 without iteration.
// The public key must have a sign value of 0x02 (i.e. must be even y-valued).
bool create_ephemeral_key(ec_secret& out_secret, data_chunk const& seed) {
    static data_chunk const magic(to_chunk("Stealth seed"));
    auto nonced_seed = build_chunk({to_array(0), seed});
    ec_compressed point;

    // Iterate up to 255 times before giving up on finding a valid key pair.
    // This gives extremely high success probability given even distribution.
    for (uint8_t nonce = 0; nonce < max_uint8; ++nonce) {
        nonced_seed[0] = nonce;
        out_secret = hmac_sha256_hash(nonced_seed, magic);

        if (secret_to_public(point, out_secret) && is_even_key(point)) {
            return true;
        }
    }

    out_secret = null_hash;
    return false;
}

// Mine a filter into the leftmost bytes of sha256(sha256(output-script)).
bool create_stealth_data(script& out_null_data, ec_secret& out_secret, binary const& filter, data_chunk const& seed) {
    // Create a valid ephemeral key pair using the seed and then the script.
    return create_ephemeral_key(out_secret, seed) &&
           create_stealth_script(out_null_data, out_secret, filter, seed);
}

// Mine a filter into the leftmost bytes of sha256(sha256(output-script)).
bool create_stealth_script(script& out_null_data, ec_secret const& secret, binary const& filter, data_chunk const& seed) {
    // [ephemeral-public-key-hash:32][pad:0-44][nonce:4]
    static constexpr
    size_t max_pad_size = max_null_data_size - hash_size -
                                           sizeof(uint32_t);

    // Derive our initial nonce and pad from the provided seed.
    auto const bytes = sha512_hash(seed);

    // Create a pad size of 0-44 using the last of bytes (avoiding pad/nonce).
    auto const pad_size = bytes.back() % max_pad_size;

    // Allocate data of target size (36-80 bytes)
    data_chunk data(hash_size + pad_size + sizeof(uint32_t));

    // Obtain the ephemeral public key from the provided ephemeral secret key.
    ec_compressed point;
    if ( ! secret_to_public(point, secret) || !is_even_key(point)) {
        return false;
    }

    // Copy the unsigned portion of the ephemeral public key into data.
    std::copy_n(point.begin() + 1, ec_compressed_size - 1, data.begin());

    // Copy arbitrary pad bytes into data.
    auto const pad_begin = data.begin() + hash_size;
    std::copy_n(bytes.begin(), pad_size, pad_begin);

    // Create an initial 32 bit nonce value from last word (avoiding pad).
    // Safe: bytes is 64 bytes (sha512), max_pad_size is 44, we read 4 bytes: 44 + 4 = 48 < 64
    auto const start = from_little_endian_unsafe<uint32_t>(std::span{bytes}.subspan(max_pad_size));

    // Mine a prefix into the double sha256 hash of the stealth script.
    // This will iterate up to 2^32 times before giving up.
    uint32_t field;
    for (uint32_t nonce = start + 1; nonce != start; ++nonce) {
        // Fill the nonce into the data buffer.
        auto const fill = to_little_endian(nonce);
        std::copy_n(fill.begin(), sizeof(nonce), data.end() - sizeof(nonce));

        // Create the stealth script with the current data.
        out_null_data = script(script::to_null_data_pattern(data));

        // Test for match of filter to stealth script hash prefix.
        if (to_stealth_prefix(field, out_null_data) &&
            filter.is_prefix_of(field)) {
            return true;
        }
    }

    out_null_data.clear();
    return false;
}

bool extract_ephemeral_key(ec_compressed& out_ephemeral_public_key,
                           script const& script) {
    if ( ! is_stealth_script(script)) {
        return false;
    }

    // The sign of the ephemeral public key is fixed by convention.
    // This requires the spender to generate a compliant (y-even) key.
    // That requires iteration with probability of 1 in 2 chance of success.
    out_ephemeral_public_key[0] = ec_even_sign;

    auto const& data = script[1].data();
    std::copy_n(data.begin(), hash_size, out_ephemeral_public_key.begin() + 1);
    return true;
}

bool extract_ephemeral_key(hash_digest& out_unsigned_ephemeral_key,
                           script const& script) {
    if ( ! is_stealth_script(script)) {
        return false;
    }

    auto const& data = script[1].data();
    std::copy_n(data.begin(), hash_size, out_unsigned_ephemeral_key.begin());
    return true;
}

bool shared_secret(ec_secret& out_shared, ec_secret const& secret, ec_compressed const& point) {
    auto copy = point;
    if ( ! ec_multiply(copy, secret)) {
        return false;
    }

    out_shared = sha256_hash(copy);
    return true;
}

bool uncover_stealth(ec_compressed& out_stealth,
                     ec_compressed const& ephemeral_or_scan,
                     ec_secret const& scan_or_ephemeral,
                     ec_compressed const& spend) {
    ec_secret shared;
    if ( ! shared_secret(shared, scan_or_ephemeral, ephemeral_or_scan)) {
        return false;
    }

    auto copy = spend;
    if ( ! ec_add(copy, shared)) {
        return false;
    }

    out_stealth = copy;
    return true;
}

bool uncover_stealth(ec_secret& out_stealth,
                     ec_compressed const& ephemeral_or_scan,
                     ec_secret const& scan_or_ephemeral,
                     ec_secret const& spend) {
    ec_secret shared;
    if ( ! shared_secret(shared, scan_or_ephemeral, ephemeral_or_scan)) {
        return false;
    }

    auto copy = spend;
    if ( ! ec_add(copy, shared)) {
        return false;
    }

    out_stealth = copy;
    return true;
}

} // namespace kth::domain
