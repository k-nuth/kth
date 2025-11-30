// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/wallet/hd_public.hpp>

#include <cstdint>
#include <iostream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
// #include <kth/infrastructure/wallet/ec_public.hpp>
#include <kth/infrastructure/wallet/hd_private.hpp>

namespace kth::infrastructure::wallet {

// const uint32_t hd_public::mainnet = 76067358;
// const uint32_t hd_public::testnet = 70617039;

// hd_public
// ----------------------------------------------------------------------------

hd_public::hd_public()
    : chain_(null_hash), lineage_({0, 0, 0, 0})
    , point_(null_compressed_point)
{}

// This cannot validate the version.
hd_public::hd_public(hd_key const& public_key)
    : hd_public(from_key(public_key))
{}

// This cannot validate the version.
hd_public::hd_public(std::string const& encoded)
    : hd_public(from_string(encoded))
{}

// This validates the version.
hd_public::hd_public(hd_key const& public_key, uint32_t prefix)
    : hd_public(from_key(public_key, prefix))
{}

// This validates the version.
hd_public::hd_public(std::string const& encoded, uint32_t prefix)
    : hd_public(from_string(encoded, prefix))
{}

hd_public::hd_public(const ec_compressed& point, const hd_chain_code& chain_code, hd_lineage const& lineage)
    : valid_(true), point_(point), chain_(chain_code), lineage_(lineage)
{}

// Factories.
// ----------------------------------------------------------------------------

hd_public hd_public::from_secret(ec_secret const& secret, const hd_chain_code& chain_code, hd_lineage const& lineage) {
    ec_compressed point;
    return secret_to_public(point, secret) ? hd_public(point, chain_code, lineage) : hd_public{};
}

hd_public hd_public::from_key(hd_key const& key) {
    auto const prefix = from_big_endian_unsafe<uint32_t>(key.begin());
    return from_key(key, prefix);
}

hd_public hd_public::from_string(std::string const& encoded) {
    hd_key key;
    if ( ! decode_base58(key, encoded)) {
        return {};
    }

    return hd_public(from_key(key));
}

hd_public hd_public::from_key(hd_key const& key, uint32_t prefix) {
    byte_reader reader(key);

    auto const actual_prefix = reader.read_big_endian<uint32_t>();
    if ( ! actual_prefix || *actual_prefix != prefix) {
        return {};
    }

    auto const depth = reader.read_byte();
    if ( ! depth) {
        return {};
    }

    auto const parent = reader.read_big_endian<uint32_t>();
    if ( ! parent) {
        return {};
    }

    auto const child = reader.read_big_endian<uint32_t>();
    if ( ! child) {
        return {};
    }

    auto const chain = reader.read_packed<hd_chain_code>();
    if ( ! chain) {
        return {};
    }

    auto const point = reader.read_packed<ec_compressed>();
    if ( ! point) {
        return {};
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

hd_public hd_public::from_string(std::string const& encoded, uint32_t prefix) {
    hd_key key;
    if ( ! decode_base58(key, encoded)) {
        return {};
    }

    return hd_public(from_key(key, prefix));
}

// Cast operators.
// ----------------------------------------------------------------------------

hd_public::operator bool const() const {
    return valid_;
}

hd_public::operator const ec_compressed&() const {
    return point_;
}

// Serializer.
// ----------------------------------------------------------------------------

std::string hd_public::encoded() const {
    return encode_base58(to_hd_key());
}

// Accessors.
// ----------------------------------------------------------------------------

const hd_chain_code& hd_public::chain_code() const {
    return chain_;
}

hd_lineage const& hd_public::lineage() const {
    return lineage_;
}

const ec_compressed& hd_public::point() const {
    return point_;
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

hd_public hd_public::derive_public(uint32_t index) const {
    if (index >= hd_first_hardened_key) {
        return {};
    }

    auto const data = splice(point_, to_big_endian(index));
    auto const intermediate = split(hmac_sha512_hash(data, chain_));

    // The returned child key Ki is point(parse256(IL)) + Kpar.
    auto combined = point_;
    if ( ! ec_add(combined, intermediate.left)) {
        return {};
    }

    if (lineage_.depth == max_uint8) {
        return {};
    }

    hd_lineage const lineage {
        lineage_.prefixes,
        static_cast<uint8_t>(lineage_.depth + 1),
        fingerprint(),
        index
    };

    return hd_public(combined, intermediate.right, lineage);
}

// Helpers.
// ----------------------------------------------------------------------------

uint32_t hd_public::fingerprint() const {
    auto const message_digest = bitcoin_short_hash(point_);
    return from_big_endian_unsafe<uint32_t>(message_digest.begin());
}

// Operators.
// ----------------------------------------------------------------------------

hd_public& hd_public::operator=(hd_public const& x) = default;

bool hd_public::operator<(hd_public const& x) const {
    return encoded() < x.encoded();
}

bool hd_public::operator==(hd_public const& x) const {
    return valid_ == x.valid_ && chain_ == x.chain_ &&
        lineage_ == x.lineage_ && point_ == x.point_;
}

bool hd_public::operator!=(hd_public const& x) const {
    return !(*this == x);
}

std::istream& operator>>(std::istream& in, hd_public& to) {
    std::string value;
    in >> value;
    to = hd_public(value);

    if ( ! to) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
        throw std::invalid_argument(value);
#endif
    }

    return in;
}

std::ostream& operator<<(std::ostream& out, hd_public const& of) {
    out << of.encoded();
    return out;
}

// hd_lineage
// ----------------------------------------------------------------------------

bool hd_lineage::operator == (hd_lineage const& x) const {
    return prefixes == x.prefixes && depth == x.depth &&
        parent_fingerprint == x.parent_fingerprint &&
        child_number == x.child_number;
}

bool hd_lineage::operator!=(hd_lineage const& x) const {
    return !(*this == x);
}

} // namespace kth::infrastructure::wallet
