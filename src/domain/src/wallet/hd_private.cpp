// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/hd_private.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::domain::wallet {

hd_private::hd_private(hd_public base, ec_secret const& secret)
    : hd_public(std::move(base))
    , secret_(secret)
{}

// Factories.
// ----------------------------------------------------------------------------

// static
expect<hd_private> hd_private::parse_from(std::string_view encoded) {
    return parse_from_with_public_prefix(encoded, hd_public::mainnet);
}

// static
expect<hd_private> hd_private::parse_from_with_public_prefix(std::string_view encoded, uint32_t public_prefix) {
    hd_key key;
    if ( ! decode_base58(key, std::string{encoded})) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_key_impl(key, public_prefix);
}

// static
expect<hd_private> hd_private::parse_from_with_prefixes(std::string_view encoded, uint64_t prefixes) {
    hd_key key;
    if ( ! decode_base58(key, std::string{encoded})) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_key_impl(key, prefixes);
}

// static
expect<hd_private> hd_private::from_seed(data_chunk const& seed, uint64_t prefixes) {
    return from_seed_impl(seed, prefixes);
}

// static
expect<hd_private> hd_private::from_hd_key(hd_key const& private_key) {
    return from_key_impl(private_key, hd_public::mainnet);
}

// static
expect<hd_private> hd_private::from_hd_key_with_public_prefix(hd_key const& private_key, uint32_t public_prefix) {
    return from_key_impl(private_key, public_prefix);
}

// static
expect<hd_private> hd_private::from_hd_key_with_prefixes(hd_key const& private_key, uint64_t prefixes) {
    return from_key_impl(private_key, prefixes);
}

// static
expect<hd_private> hd_private::from_verified_secret(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage) {
    auto base = hd_public::from_secret(secret, chain_code, lineage);
    if ( ! base) {
        return std::unexpected(base.error());
    }
    return hd_private(std::move(*base), secret);
}

// static
expect<hd_private> hd_private::from_seed_impl(byte_span seed, uint64_t prefixes) {
    // This is a magic constant from BIP32.
    static data_chunk const magic(to_chunk("Bitcoin seed"));

    auto const intermediate = split(hmac_sha512_hash(seed, magic));

    // The key is invalid if parse256(IL) >= n or 0:
    if ( ! verify(intermediate.left)) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const master = hd_lineage {
        prefixes,
        0x00,
        0x00000000,
        0x00000000
    };

    return from_verified_secret(intermediate.left, intermediate.right, master);
}

// static
expect<hd_private> hd_private::from_key_impl(hd_key const& key, uint32_t public_prefix) {
    auto const prefix = from_big_endian_unsafe<uint32_t>(key);
    return from_key_impl(key, to_prefixes(prefix, public_prefix));
}

// static
expect<hd_private> hd_private::from_key_impl(hd_key const& key, uint64_t prefixes) {
    byte_reader reader(key);

    auto const prefix = reader.read_big_endian<uint32_t>();
    if ( ! prefix || *prefix != to_prefix(prefixes)) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const depth = reader.read_byte();
    if ( ! depth) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const parent = reader.read_big_endian<uint32_t>();
    if ( ! parent) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const child = reader.read_big_endian<uint32_t>();
    if ( ! child) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const chain = reader.read_packed<hd_chain_code>();
    if ( ! chain) {
        return std::unexpected(kth::error::illegal_value);
    }

    if ( ! reader.skip(1)) {  // padding byte
        return std::unexpected(kth::error::illegal_value);
    }

    auto const secret = reader.read_packed<ec_secret>();
    if ( ! secret) {
        return std::unexpected(kth::error::illegal_value);
    }

    hd_lineage const lineage {
        prefixes,
        *depth,
        *parent,
        *child
    };

    return from_verified_secret(*secret, *chain, lineage);
}

// Serializer.
// ----------------------------------------------------------------------------

std::string hd_private::to_string() const {
    return encode_base58(to_hd_key());
}

// Methods.
// ----------------------------------------------------------------------------

// HD keys do not carry a payment address prefix (just like WIF).
// So we are currently not converting to ec_public or ec_private.

hd_key hd_private::to_hd_key() const {
    static constexpr uint8_t private_key_padding = 0x00;

    hd_key out;
    build_checked_array(out,
    {
        to_big_endian(to_prefix(lineage_.prefixes)),
        to_array(lineage_.depth),
        to_big_endian(lineage_.parent_fingerprint),
        to_big_endian(lineage_.child_number),
        chain_,
        to_array(private_key_padding),
        secret_
    });

    return out;
}

hd_public hd_private::to_public() const {
    // Swap the private-side prefix out for the public-side prefix
    // stored in the low 32 bits, then build directly from the
    // already-derived point (no serialization round-trip).
    hd_lineage adjusted = lineage_;
    adjusted.prefixes = hd_public::to_prefix(lineage_.prefixes);
    return hd_public(point_, chain_, adjusted);
}

expect<hd_private> hd_private::derive_private(uint32_t index) const {
    constexpr uint8_t depth = 0;

    auto const data = (index >= hd_first_hardened_key) ?
        splice(to_array(depth), secret_, to_big_endian(index)) :
        splice(point_, to_big_endian(index));

    auto const intermediate = split(hmac_sha512_hash(data, chain_));

    // The child key ki is (parse256(IL) + kpar) mod n:
    auto child = secret_;
    if ( ! ec_add(child, intermediate.left)) {
        return std::unexpected(kth::error::illegal_value);
    }

    if (lineage_.depth == max_uint8) {
        return std::unexpected(kth::error::illegal_value);
    }

    hd_lineage const lineage {
        lineage_.prefixes,
        uint8_t(lineage_.depth + 1),
        fingerprint(),
        index
    };

    return from_verified_secret(child, intermediate.right, lineage);
}

expect<hd_public> hd_private::derive_public(uint32_t index) const {
    auto priv = derive_private(index);
    if ( ! priv) {
        return std::unexpected(priv.error());
    }
    return priv->to_public();
}

void hd_private::wipe() noexcept {
    hd_public::wipe();
    secret_.fill(0);
}

} // namespace kth::domain::wallet
