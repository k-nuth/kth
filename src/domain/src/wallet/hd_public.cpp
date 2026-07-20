// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/hd_public.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/hd_private.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::domain::wallet {

// hd_public
// ----------------------------------------------------------------------------

// static
expect<hd_public> hd_public::parse_from(std::string_view encoded) {
    auto const key = decode_base58<hd_key_size>(encoded);
    if ( ! key) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_hd_key(*key);
}

// static
expect<hd_public> hd_public::parse_from_with_prefix(std::string_view encoded, uint32_t prefix) {
    auto const key = decode_base58<hd_key_size>(encoded);
    if ( ! key) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_hd_key_with_prefix(*key, prefix);
}

// static
expect<hd_public> hd_public::from_hd_key(hd_key const& key) {
    return from_key_impl(key);
}

// static
expect<hd_public> hd_public::from_hd_key_with_prefix(hd_key const& key, uint32_t prefix) {
    return from_key_impl(key, prefix);
}

// Factories.
// ----------------------------------------------------------------------------

// static
expect<hd_public> hd_public::from_secret(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage) {
    ec_compressed point;
    if ( ! secret_to_public(point, secret)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return hd_public(point, chain_code, lineage);
}

// static
expect<hd_public> hd_public::from_key_impl(hd_key const& key) {
    auto const prefix = from_big_endian_unsafe<uint32_t>(key);
    return from_key_impl(key, prefix);
}

// static
expect<hd_public> hd_public::from_key_impl(hd_key const& key, uint32_t prefix) {
    byte_reader reader(key);

    auto const actual_prefix = reader.read_big_endian<uint32_t>();
    if ( ! actual_prefix || *actual_prefix != prefix) {
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

    auto const point = reader.read_packed<ec_compressed>();
    if ( ! point) {
        return std::unexpected(kth::error::illegal_value);
    }

    // The private prefix will be zero'd here, but there's no way to access it.
    hd_lineage const lineage {
        prefix,
        *depth,
        *parent,
        *child
    };

    return hd_public(*point, *chain, lineage);
}

// Serializer.
// ----------------------------------------------------------------------------

std::string hd_public::to_string() const {
    return encode_base58(to_hd_key());
}

// Methods.
// ----------------------------------------------------------------------------

// HD keys do not carry a payment address prefix (just like WIF).
// So we are currently not converting to ec_public or ec_private.

hd_key hd_public::to_hd_key() const {
    hd_key out;
    build_checked_array(out,
    {
        to_big_endian(to_prefix(lineage_.prefixes)),
        to_array(lineage_.depth),
        to_big_endian(lineage_.parent_fingerprint),
        to_big_endian(lineage_.child_number),
        chain_,
        point_
    });

    return out;
}

expect<hd_public> hd_public::derive_public(uint32_t index) const {
    if (index >= hd_first_hardened_key) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const data = splice(point_, to_big_endian(index));
    auto const intermediate = split(hmac_sha512_hash(data, chain_));

    // The returned child key Ki is point(parse256(IL)) + Kpar.
    auto combined = point_;
    if ( ! ec_add(combined, intermediate.left)) {
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

    return hd_public(combined, intermediate.right, lineage);
}

void hd_public::wipe() noexcept {
    chain_.fill(0);
    lineage_ = {0, 0, 0, 0};
    point_.fill(0);
}

// Helpers.
// ----------------------------------------------------------------------------

uint32_t hd_public::fingerprint() const {
    auto const message_digest = bitcoin_short_hash(point_);
    return from_big_endian_unsafe<uint32_t>(message_digest);
}

} // namespace kth::domain::wallet
