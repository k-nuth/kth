// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/encrypted_keys.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <boost/locale.hpp>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/crypto.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

#include "parse_encrypted_keys/parse_encrypted_key.hpp"
#include "parse_encrypted_keys/parse_encrypted_prefix.hpp"
#include "parse_encrypted_keys/parse_encrypted_private.hpp"
#include "parse_encrypted_keys/parse_encrypted_public.hpp"
#include "parse_encrypted_keys/parse_encrypted_token.hpp"

namespace kth::domain::wallet {

// Alias commonly-used constants for brevity.
static constexpr auto half = half_hash_size;
static constexpr auto quarter = quarter_hash_size;

// Ensure that hash sizes are aligned with AES block size.
static_assert(2 * quarter == kth::aes256_block_size, "oops!");

// address_
// ----------------------------------------------------------------------------

static
hash_digest address_hash(payment_address const& address) {
    return bitcoin_hash(to_chunk(address.encoded_legacy()));
}

static
bool address_salt(ek_salt& salt, payment_address const& address) {
    salt = slice<0, ek_salt_size>(address_hash(address));
    return true;
}

static
bool address_salt(ek_salt& salt, ec_compressed const& point, uint8_t version, bool compressed) {
    payment_address address(ec_public{point, compressed}, version);
    return address ? address_salt(salt, address) : false;
}

static
bool address_salt(ek_salt& salt, ec_secret const& secret, uint8_t version, bool compressed) {
    payment_address address(ec_private{secret, version, compressed});
    return address ? address_salt(salt, address) : false;
}

static
bool address_validate(ek_salt const& salt, payment_address const& address) {
    auto const hash = address_hash(address);
    return std::equal(hash.begin(), hash.begin() + salt.size(), salt.begin());
}

static
bool address_validate(ek_salt const& salt, ec_compressed const& point, uint8_t version, bool compressed) {
    payment_address address(ec_public{point, compressed}, version);
    return address ? address_validate(salt, address) : false;
}

static
bool address_validate(ek_salt const& salt, ec_secret const& secret, uint8_t version, bool compressed) {
    payment_address address(ec_private{secret, version, compressed});
    return address ? address_validate(salt, address) : false;
}

// point_
// ----------------------------------------------------------------------------

static
hash_digest point_hash(ec_compressed const& point) {
    return slice<1, ec_compressed_size>(point);
}

static
one_byte point_sign(uint8_t byte, hash_digest const& hash) {
    static constexpr uint8_t low_bit_mask = 0x01;
    uint8_t const last_byte = hash.back();
    uint8_t const last_byte_odd_field = last_byte & low_bit_mask;
    uint8_t const sign_byte = byte ^ last_byte_odd_field;
    return to_array(sign_byte);
}

static
one_byte point_sign(one_byte const& single, hash_digest const& hash) {
    return point_sign(single.front(), hash);
}

// scrypt_
// ----------------------------------------------------------------------------

static
hash_digest scrypt_token(byte_span data, byte_span salt) {
    // Arbitrary scrypt parameters from BIP38.
    return scrypt<hash_size>(data, salt, 16384u, 8u, 8u);
}

static
long_hash scrypt_pair(byte_span data, byte_span salt) {
    // Arbitrary scrypt parameters from BIP38.
    return scrypt<long_hash_size>(data, salt, 1024u, 1u, 1u);
}

static
long_hash scrypt_private(byte_span data, byte_span salt) {
    // Arbitrary scrypt parameters from BIP38.
    return scrypt<long_hash_size>(data, salt, 16384u, 8u, 8u);
}

// set_flags
// ----------------------------------------------------------------------------

static
one_byte set_flags(bool compressed, bool lot_sequence, bool multiplied) {
    uint8_t byte = 0;

    if (compressed) {
        byte |= ek_flag::ec_compressed_key;
    }

    if (lot_sequence) {
        byte |= ek_flag::lot_sequence_key;
    }

    if ( ! multiplied) {
        byte |= ek_flag::ec_non_multiplied;
    }

    return to_array(byte);
}

static
one_byte set_flags(bool compressed, bool lot_sequence) {
    return set_flags(compressed, lot_sequence, false);
}

static
one_byte set_flags(bool compressed) {
    return set_flags(compressed, false);
}

// create_key_pair
// ----------------------------------------------------------------------------

static
void create_private_key(encrypted_private& out_private,
                               one_byte const& flags,
                               ek_salt const& salt,
                               ek_entropy const& entropy,
                               hash_digest const& derived1,
                               hash_digest const& derived2,
                               ek_seed const& seed,
                               uint8_t version) {
    auto const prefix = parse_encrypted_private::prefix_factory(version, true);

    auto encrypt1 = xor_data<half>(seed, derived1);
    aes256_encrypt(derived2, encrypt1);
    auto const combined = splice(slice<quarter, half>(encrypt1),
                                 slice<half, half + quarter>(seed));

    auto encrypt2 = xor_data<half>(combined, derived1, 0, half);
    aes256_encrypt(derived2, encrypt2);
    auto const quarter1 = slice<0, quarter>(encrypt1);

    build_checked_array(out_private,
                        {prefix,
                         flags,
                         salt,
                         entropy,
                         quarter1,
                         encrypt2});
}

static
bool create_public_key(encrypted_public& out_public,
                              one_byte const& flags,
                              ek_salt const& salt,
                              ek_entropy const& entropy,
                              hash_digest const& derived1,
                              hash_digest const& derived2,
                              ec_secret const& secret,
                              uint8_t version) {
    ec_compressed point;
    if ( ! secret_to_public(point, secret)) {
        return false;
    }

    auto const prefix = parse_encrypted_public::prefix_factory(version);
    auto const hash = point_hash(point);

    auto encrypted1 = xor_data<half>(hash, derived1);
    aes256_encrypt(derived2, encrypted1);

    auto encrypted2 = xor_data<half>(hash, derived1, half);
    aes256_encrypt(derived2, encrypted2);

    auto const sign = point_sign(point.front(), derived2);
    return build_checked_array(out_public,
                               {prefix,
                                flags,
                                salt,
                                entropy,
                                sign,
                                encrypted1,
                                encrypted2});
}

// There is no scenario requiring a public key, we support it for completeness.
bool create_key_pair(encrypted_private& out_private,
                     encrypted_public& out_public,
                     ec_compressed& out_point,
                     encrypted_token const& token,
                     ek_seed const& seed,
                     uint8_t version,
                     bool compressed) {
    parse_encrypted_token const parse(token);
    if ( ! parse.valid()) {
        return false;
    }

    auto const point = splice(parse.sign(), parse.data());
    auto point_copy = point;
    auto const factor = bitcoin_hash(seed);
    if ( ! ec_multiply(point_copy, factor)) {
        return false;
    }

    ek_salt salt;
    if ( ! address_salt(salt, point_copy, version, compressed)) {
        return false;
    }

    auto const salt_entropy = splice(salt, parse.entropy());
    auto const derived = split(scrypt_pair(point, salt_entropy));
    auto const flags = set_flags(compressed, parse.lot_sequence(), true);

    if ( ! create_public_key(out_public, flags, salt, parse.entropy(),
                           derived.left, derived.right, factor, version)) {
        return false;
    }

    create_private_key(out_private, flags, salt, parse.entropy(), derived.left,
                       derived.right, seed, version);

    out_point = point_copy;
    return true;
}

bool create_key_pair(encrypted_private& out_private, ec_compressed& out_point, encrypted_token const& token, ek_seed const& seed, uint8_t version, bool compressed) {
    encrypted_public out_public;
    return create_key_pair(out_private, out_public, out_point, token, seed,
                           version, compressed);
}

#ifdef WITH_ICU

// create_token
// ----------------------------------------------------------------------------

// This call requires an ICU build, the other excluded calls are dependencies.
static
data_chunk normal(std::string const& passphrase) {
    return to_chunk(to_normal_nfc_form(passphrase));
}

static
bool create_token(encrypted_token& out_token,
                         std::string const& passphrase,
                         byte_span owner_salt,
                         ek_entropy const& owner_entropy,
                         byte_array<parse_encrypted_token::prefix_size> const& prefix) {
    KTH_ASSERT(owner_salt.size() == ek_salt_size ||
                   owner_salt.size() == ek_entropy_size);

    auto const lot_sequence = owner_salt.size() == ek_salt_size;
    auto factor = scrypt_token(normal(passphrase), owner_salt);

    if (lot_sequence)
        factor = bitcoin_hash(splice(factor, owner_entropy));

    ec_compressed point;
    if ( ! secret_to_public(point, factor))
        return false;

    return build_checked_array(out_token,
                               {prefix,
                                owner_entropy,
                                point});
}

// The salt here is owner-supplied random bits, not the address hash.
bool create_token(encrypted_token& out_token, std::string const& passphrase, ek_entropy const& entropy) {
    // BIP38: If lot and sequence numbers are not being included, then
    // owner_salt is 8 random bytes instead of 4, lot_sequence is omitted and
    // owner_entropy becomes an alias for owner_salt.
    auto const prefix = parse_encrypted_token::prefix_factory(false);
    return create_token(out_token, passphrase, entropy, entropy, prefix);
}

// The salt here is owner-supplied random bits, not the address hash.
bool create_token(encrypted_token& out_token, std::string const& passphrase, ek_salt const& salt, uint32_t lot, uint32_t sequence) {
    if (lot > ek_max_lot || sequence > ek_max_sequence)
        return false;

    static constexpr size_t max_sequence_bits = 12;
    uint32_t const lot_sequence = (lot << max_sequence_bits) || sequence;
    auto const entropy = splice(salt, to_big_endian(lot_sequence));
    auto const prefix = parse_encrypted_token::prefix_factory(true);
    create_token(out_token, passphrase, salt, entropy, prefix);
    return true;
}

// encrypt
// ----------------------------------------------------------------------------

bool encrypt(encrypted_private& out_private, ec_secret const& secret, std::string const& passphrase, uint8_t version, bool compressed) {
    ek_salt salt;
    if ( ! address_salt(salt, secret, version, compressed))
        return false;

    auto const derived = split(scrypt_private(normal(passphrase), salt));
    auto const prefix = parse_encrypted_private::prefix_factory(version,
                                                                false);

    auto encrypted1 = xor_data<half>(secret, derived.left);
    aes256_encrypt(derived.right, encrypted1);

    auto encrypted2 = xor_data<half>(secret, derived.left, half);
    aes256_encrypt(derived.right, encrypted2);

    return build_checked_array(out_private,
                               {prefix,
                                set_flags(compressed),
                                salt,
                                encrypted1,
                                encrypted2});
}

// decrypt private_key
// ----------------------------------------------------------------------------

static
bool decrypt_multiplied(ec_secret& out_secret,
                               parse_encrypted_private const& parse,
                               std::string const& passphrase) {
    auto secret = scrypt_token(normal(passphrase), parse.owner_salt());

    if (parse.lot_sequence())
        secret = bitcoin_hash(splice(secret, parse.entropy()));

    ec_compressed point;
    if ( ! secret_to_public(point, secret))
        return false;

    auto const salt_entropy = splice(parse.salt(), parse.entropy());
    auto const derived = split(scrypt_pair(point, salt_entropy));

    auto encrypt1 = parse.data1();
    auto encrypt2 = parse.data2();

    aes256_decrypt(derived.right, encrypt2);
    auto const decrypt2 = xor_data<half>(encrypt2, derived.left, 0, half);
    auto part = split(decrypt2);
    auto extended = splice(encrypt1, part.left);

    aes256_decrypt(derived.right, extended);
    auto const decrypt1 = xor_data<half>(extended, derived.left);
    auto const factor = bitcoin_hash(splice(decrypt1, part.right));
    if ( ! ec_multiply(secret, factor))
        return false;

    auto const compressed = parse.compressed();
    auto const address_version = parse.address_version();
    if ( ! address_validate(parse.salt(), secret, address_version, compressed))
        return false;

    out_secret = secret;
    return true;
}

static
bool decrypt_secret(ec_secret& out_secret,
                           parse_encrypted_private const& parse,
                           std::string const& passphrase) {
    auto encrypt1 = splice(parse.entropy(), parse.data1());
    auto encrypt2 = parse.data2();
    auto const derived = split(scrypt_private(normal(passphrase),
                                              parse.salt()));

    aes256_decrypt(derived.right, encrypt1);
    aes256_decrypt(derived.right, encrypt2);

    auto const encrypted = splice(encrypt1, encrypt2);
    auto const secret = xor_data<hash_size>(encrypted, derived.left);

    auto const compressed = parse.compressed();
    auto const address_version = parse.address_version();
    if ( ! address_validate(parse.salt(), secret, address_version, compressed))
        return false;

    out_secret = secret;
    return true;
}

bool decrypt(ec_secret& out_secret, uint8_t& out_version, bool& out_compressed, encrypted_private const& key, std::string const& passphrase) {
    parse_encrypted_private const parse(key);
    if ( ! parse.valid())
        return false;

    auto const success = parse.multiplied() ? decrypt_multiplied(out_secret, parse, passphrase) : decrypt_secret(out_secret, parse, passphrase);

    if (success) {
        out_compressed = parse.compressed();
        out_version = parse.address_version();
    }

    return success;
}

// decrypt public_key
// ----------------------------------------------------------------------------

bool decrypt(ec_compressed& out_point, uint8_t& out_version, bool& out_compressed, encrypted_public const& key, std::string const& passphrase) {
    parse_encrypted_public const parse(key);
    if ( ! parse.valid())
        return false;

    auto const version = parse.address_version();
    auto const lot_sequence = parse.lot_sequence();
    auto factor = scrypt_token(normal(passphrase), parse.owner_salt());

    if (lot_sequence)
        factor = bitcoin_hash(splice(factor, parse.entropy()));

    ec_compressed point;
    if ( ! secret_to_public(point, factor))
        return false;

    auto const salt_entropy = splice(parse.salt(), parse.entropy());
    auto derived = split(scrypt_pair(point, salt_entropy));
    auto encrypt = split(parse.data());

    aes256_decrypt(derived.right, encrypt.left);
    auto const decrypt1 = xor_data<half>(encrypt.left, derived.left);

    aes256_decrypt(derived.right, encrypt.right);
    auto const decrypt2 = xor_data<half>(encrypt.right, derived.left, 0, half);

    auto const sign_byte = point_sign(parse.sign(), derived.right);
    auto product = splice(sign_byte, decrypt1, decrypt2);
    if ( ! ec_multiply(product, factor))
        return false;

    if ( ! address_validate(parse.salt(), product, version, parse.compressed()))
        return false;

    out_point = product;
    out_version = version;
    out_compressed = parse.compressed();
    return true;
}

#endif  // WITH_ICU

} // namespace kth::domain::wallet
