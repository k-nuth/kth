// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/elliptic_curve.hpp>

#include <algorithm>
#include <utility>

#include "../math/external/lax_der_parsing.h"

#include "secp256k1_initializer.hpp"
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <secp256k1_schnorr.h>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth {

static constexpr uint8_t compressed_even = 0x02;
static constexpr uint8_t compressed_odd = 0x03;
static constexpr uint8_t uncompressed = 0x04;

constexpr
int to_flags(bool compressed) {
    return compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED;
}

// Helper templates
// ----------------------------------------------------------------------------
// These allow strong typing of private keys without redundant code.

template <size_t Size>
bool parse(const secp256k1_context* context, secp256k1_pubkey& out, const byte_array<Size>& point) {
    return secp256k1_ec_pubkey_parse(context, &out, point.data(), Size) == 1;
}

template <size_t Size>
bool serialize(const secp256k1_context* context, byte_array<Size>& out, const secp256k1_pubkey point) {
    auto size = Size;
    auto const flags = to_flags(Size == ec_compressed_size);
    secp256k1_ec_pubkey_serialize(context, out.data(), &size, &point, flags);
    return size == Size;
}

template <size_t Size>
bool ec_add(const secp256k1_context* context, byte_array<Size>& in_out, ec_secret const& secret) {
    secp256k1_pubkey pubkey;
    return parse(context, pubkey, in_out) &&
        secp256k1_ec_pubkey_tweak_add(context, &pubkey, secret.data()) == 1 &&
        serialize(context, in_out, pubkey);
}

template <size_t Size>
bool ec_multiply(const secp256k1_context* context, byte_array<Size>& in_out, ec_secret const& secret) {
    secp256k1_pubkey pubkey;
    return parse(context, pubkey, in_out) &&
        secp256k1_ec_pubkey_tweak_mul(context, &pubkey, secret.data()) == 1 &&
        serialize(context, in_out, pubkey);
}

template <size_t Size>
bool secret_to_public(const secp256k1_context* context, byte_array<Size>& out, ec_secret const& secret) {
    secp256k1_pubkey pubkey;
    return secp256k1_ec_pubkey_create(context, &pubkey, secret.data()) == 1 &&
        serialize(context, out, pubkey);
}

template <size_t Size>
bool recover_public(const secp256k1_context* context, byte_array<Size>& out, const recoverable_signature& recoverable, hash_digest const& hash) {
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_recoverable_signature sign;
    auto const recovery_id = safe_to_signed<int>(recoverable.recovery_id);
    if ( ! recovery_id) {
        return false;
    }
    return secp256k1_ecdsa_recoverable_signature_parse_compact(context, &sign, recoverable.signature.data(), *recovery_id) == 1 &&
           secp256k1_ecdsa_recover(context, &pubkey, &sign, hash.data()) == 1 &&
           serialize(context, out, pubkey);
}

bool verify_signature(const secp256k1_context* context,
    const secp256k1_pubkey point, hash_digest const& hash,
    const ec_signature& signature) {

    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature parsed;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(parsed.data));

    // secp256k1_ecdsa_verify rejects non-normalized (low-s) signatures, but
    // bitcoin does not have such a limitation, so we always normalize.
    secp256k1_ecdsa_signature normal;
    secp256k1_ecdsa_signature_normalize(context, &normal, &parsed);
    return secp256k1_ecdsa_verify(context, &normal, hash.data(), &point) == 1;
}

// Add and multiply EC values
// ----------------------------------------------------------------------------

bool ec_add(ec_compressed& point, ec_secret const& secret) {
    auto const context = verification.context();
    return ec_add(context, point, secret);
}

bool ec_add(ec_uncompressed& point, ec_secret const& secret) {
    auto const context = verification.context();
    return ec_add(context, point, secret);
}

bool ec_add(ec_secret& left, ec_secret const& right) {
    auto const context = verification.context();
    return secp256k1_ec_privkey_tweak_add(context, left.data(),
        right.data()) == 1;
}

bool ec_multiply(ec_compressed& point, ec_secret const& secret) {
    auto const context = verification.context();
    return ec_multiply(context, point, secret);
}

bool ec_multiply(ec_uncompressed& point, ec_secret const& secret) {
    auto const context = verification.context();
    return ec_multiply(context, point, secret);
}

bool ec_multiply(ec_secret& left, ec_secret const& right) {
    auto const context = verification.context();
    return secp256k1_ec_privkey_tweak_mul(context, left.data(),
        right.data()) == 1;
}

// Convert keys
// ----------------------------------------------------------------------------

bool compress(ec_compressed& out, const ec_uncompressed& point) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point) && serialize(context, out, pubkey);
}

bool decompress(ec_uncompressed& out, const ec_compressed& point) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point) && serialize(context, out, pubkey);
}

bool secret_to_public(ec_compressed& out, ec_secret const& secret) {
    auto const context = signing.context();
    return secret_to_public(context, out, secret);
}

bool secret_to_public(ec_uncompressed& out, ec_secret const& secret) {
    auto const context = signing.context();
    return secret_to_public(context, out, secret);
}

// Verify keys
// ----------------------------------------------------------------------------

bool verify(ec_secret const& secret) {
    auto const context = verification.context();
    return secp256k1_ec_seckey_verify(context, secret.data()) == 1;
}

bool verify(const ec_compressed& point) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point);
}

bool verify(const ec_uncompressed& point) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point);
}

// Detect public keys
// ----------------------------------------------------------------------------

bool is_compressed_key(byte_span point) {
    auto const size = point.size();
    if (size != ec_compressed_size) {
        return false;
    }

    auto const first = point.data()[0];
    return first == compressed_even || first == compressed_odd;
}

bool is_uncompressed_key(byte_span point) {
    auto const size = point.size();
    if (size != ec_uncompressed_size) {
        return false;
    }

    auto const first = point.data()[0];
    return first == uncompressed;
}

bool is_public_key(byte_span point) {
    return is_compressed_key(point) || is_uncompressed_key(point);
}

bool is_even_key(const ec_compressed& point) {
    return point.front() == ec_even_sign;
}

bool is_endorsement(const endorsement& endorsement) {
    auto const size = endorsement.size();
    return size >= min_endorsement_size && size <= max_endorsement_size;
}

// DER parse/encode
// ----------------------------------------------------------------------------

bool parse_endorsement(uint8_t& sighash_type, der_signature& der_signature, endorsement&& endorsement) {
    if (endorsement.empty()) {
        return false;
    }

    sighash_type = endorsement.back();
    endorsement.pop_back();
    der_signature = std::move(endorsement);
    return true;
}

bool parse_signature(ec_signature& out, const der_signature& der_signature, bool strict) {
    if (der_signature.empty()) {
        return false;
    }

    bool valid;
    secp256k1_ecdsa_signature parsed;
    auto const context = verification.context();

    if (strict) {
        valid = secp256k1_ecdsa_signature_parse_der(context, &parsed,
            der_signature.data(), der_signature.size()) == 1;
    } else {
        valid = ecdsa_signature_parse_der_lax(context, &parsed,
            der_signature.data(), der_signature.size()) == 1;
    }

    if (valid) {
        std::copy_n(std::begin(parsed.data), ec_signature_size, out.begin());
    }

    return valid;
}

bool encode_signature(der_signature& out, ec_signature const& signature) {
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature sign;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(sign.data));

    auto const context = signing.context();
    auto size = max_der_signature_size;
    out.resize(size);

    if (secp256k1_ecdsa_signature_serialize_der(context, out.data(), &size, &sign) != 1) {
        return false;
    }

    out.resize(size);
    return true;
}

bool encode_signature(compact_signature& out, ec_signature const& signature) {
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature sign;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(sign.data));

    auto const context = signing.context();

    if (secp256k1_ecdsa_signature_serialize_compact(context, out.data(), &sign) != 1) {
        return false;
    }
    return true;
}


// EC sign/verify
// ----------------------------------------------------------------------------

bool sign_ecdsa(ec_signature& out, ec_secret const& secret, hash_digest const& hash) {
    secp256k1_ecdsa_signature signature;
    auto const context = signing.context();

    if (secp256k1_ecdsa_sign(
        context,
        &signature,
        hash.data(),
        secret.data(),
        secp256k1_nonce_function_rfc6979,
        nullptr) != 1) {
        return false;
    }

    std::copy_n(std::begin(signature.data), out.size(), out.begin());
    return true;
}

bool sign_schnorr(ec_signature& out, ec_secret const& secret, hash_digest const& hash) {
    auto const context = signing.context();

    if (secp256k1_schnorr_sign(
        context,
        out.data(),
        hash.data(),
        secret.data(),
        secp256k1_nonce_function_rfc6979,
        nullptr) != 1) {
        return false;
    }

    return true;
}

bool verify_signature(const ec_compressed& point, hash_digest const& hash, const ec_signature& signature) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point) &&
        verify_signature(context, pubkey, hash, signature);
}

bool verify_signature(const ec_uncompressed& point, hash_digest const& hash, const ec_signature& signature) {
    secp256k1_pubkey pubkey;
    auto const context = verification.context();
    return parse(context, pubkey, point) &&
        verify_signature(context, pubkey, hash, signature);
}

bool verify_signature(byte_span point, hash_digest const& hash, const ec_signature& signature) {
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature parsed;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(parsed.data));

    // secp256k1_ecdsa_verify rejects non-normalized (low-s) signatures, but
    // bitcoin does not have such a limitation, so we always normalize.
    secp256k1_ecdsa_signature normal;
    auto const context = verification.context();
    secp256k1_ecdsa_signature_normalize(context, &normal, &parsed);

    // This uses a byte span and calls secp256k1_ec_pubkey_parse() in place of
    // parse() so that we can support the der_verify data_chunk optimization.
    secp256k1_pubkey pubkey;
    auto const size = point.size();
    return
        secp256k1_ec_pubkey_parse(context, &pubkey, point.data(), size) == 1 &&
        secp256k1_ecdsa_verify(context, &normal, hash.data(), &pubkey) == 1;
}

// Recoverable sign/recover
// ----------------------------------------------------------------------------

bool sign_recoverable(recoverable_signature& out, ec_secret const& secret, hash_digest const& hash) {
    int recovery_id;
    auto const context = signing.context();
    secp256k1_ecdsa_recoverable_signature signature;

    auto const result =
        secp256k1_ecdsa_sign_recoverable(context, &signature, hash.data(), secret.data(), secp256k1_nonce_function_rfc6979, nullptr) == 1 &&
        secp256k1_ecdsa_recoverable_signature_serialize_compact(context, out.signature.data(), &recovery_id, &signature) == 1;

    KTH_ASSERT(recovery_id >= 0 && recovery_id <= 3);
    auto const recovery_id_unsigned = safe_to_unsigned<uint8_t>(recovery_id);
    if ( ! recovery_id_unsigned) {
        return false;
    }
    out.recovery_id = *recovery_id_unsigned;
    return result;
}

bool recover_public(ec_compressed& out, const recoverable_signature& recoverable, hash_digest const& hash) {
    auto const context = verification.context();
    return recover_public(context, out, recoverable, hash);
}

bool recover_public(ec_uncompressed& out, const recoverable_signature& recoverable, hash_digest const& hash) {
    auto const context = verification.context();
    return recover_public(context, out, recoverable, hash);
}

} // namespace kth
