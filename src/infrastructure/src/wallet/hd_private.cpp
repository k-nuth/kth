// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/wallet/hd_private.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/serializer.hpp>
// #include <kth/infrastructure/wallet/ec_private.hpp>
// #include <kth/infrastructure/wallet/ec_public.hpp>

namespace kth::infrastructure::wallet {

hd_private::hd_private(data_chunk const& seed, uint64_t prefixes)
    : hd_private(from_seed(seed, prefixes))
{}

// This reads the private version and sets the public to mainnet.
hd_private::hd_private(hd_key const& private_key)
    : hd_private(from_key(private_key, hd_public::mainnet))
{}

// This reads the private version and sets the public to mainnet.
hd_private::hd_private(std::string const& encoded)
    : hd_private(from_string(encoded, hd_public::mainnet))
{}

// This reads the private version and sets the public.
hd_private::hd_private(hd_key const& private_key, uint32_t prefix)
    : hd_private(from_key(private_key, prefix))
{}

// This validates the private version and sets the public.
hd_private::hd_private(hd_key const& private_key, uint64_t prefixes)
    : hd_private(from_key(private_key, prefixes))
{}

// This reads the private version and sets the public.
hd_private::hd_private(std::string const& encoded, uint32_t prefix)
    : hd_private(from_string(encoded, prefix))
{}

// This validates the private version and sets the public.
hd_private::hd_private(std::string const& encoded, uint64_t prefixes)
    : hd_private(from_string(encoded, prefixes))
{}

hd_private::hd_private(ec_secret const& secret, const hd_chain_code& chain_code, hd_lineage const& lineage)
    : hd_public(from_secret(secret, chain_code, lineage))
    , secret_(secret)
{}

// Factories.
// ----------------------------------------------------------------------------

hd_private hd_private::from_seed(data_slice seed, uint64_t prefixes) {
    // This is a magic constant from BIP32.
    static data_chunk const magic(to_chunk("Bitcoin seed"));

    auto const intermediate = split(hmac_sha512_hash(seed, magic));

    // The key is invalid if parse256(IL) >= n or 0:
    if ( ! verify(intermediate.left)) {
        return {};
    }

    auto const master = hd_lineage {
        prefixes,
        0x00,
        0x00000000,
        0x00000000
    };

    return hd_private(intermediate.left, intermediate.right, master);
}

hd_private hd_private::from_key(hd_key const& key, uint32_t public_prefix) {
    auto const prefix = from_big_endian_unsafe<uint32_t>(key.begin());
    return from_key(key, to_prefixes(prefix, public_prefix));
}

hd_private hd_private::from_key(hd_key const& key, uint64_t prefixes) {
    byte_reader reader(key);

    auto const prefix = reader.read_big_endian<uint32_t>();
    if ( ! prefix || *prefix != to_prefix(prefixes)) {
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

    if ( ! reader.skip(1)) {  // padding byte
        return {};
    }

    auto const secret = reader.read_packed<ec_secret>();
    if ( ! secret) {
        return {};
    }

    hd_lineage const lineage {
        prefixes,
        *depth,
        *parent,
        *child
    };

    return hd_private(*secret, *chain, lineage);
}

hd_private hd_private::from_string(std::string const& encoded, uint32_t public_prefix) {
    hd_key key;
    if ( ! decode_base58(key, encoded)) {
        return {};
    }

    return hd_private(from_key(key, public_prefix));
}

hd_private hd_private::from_string(std::string const& encoded, uint64_t prefixes) {
    hd_key key;
    return decode_base58(key, encoded) ? hd_private(key, prefixes) :
        hd_private{};
}

// Cast operators.
// ----------------------------------------------------------------------------

hd_private::operator ec_secret const&() const {
    return secret_;
}

// Serializer.
// ----------------------------------------------------------------------------

std::string hd_private::encoded() const {
    return encode_base58(to_hd_key());
}

/// Accessors.
// ----------------------------------------------------------------------------

ec_secret const& hd_private::secret() const {
    return secret_;
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
    return hd_public(
        ((hd_public)*this).to_hd_key(),
        hd_public::to_prefix(lineage_.prefixes));
}

hd_private hd_private::derive_private(uint32_t index) const {
    constexpr uint8_t depth = 0;

    auto const data = (index >= hd_first_hardened_key) ?
        splice(to_array(depth), secret_, to_big_endian(index)) :
        splice(point_, to_big_endian(index));

    auto const intermediate = split(hmac_sha512_hash(data, chain_));

    // The child key ki is (parse256(IL) + kpar) mod n:
    auto child = secret_;
    if ( ! ec_add(child, intermediate.left)) {
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

    return hd_private(child, intermediate.right, lineage);
}

hd_public hd_private::derive_public(uint32_t index) const {
    return derive_private(index).to_public();
}

// Operators.
// ----------------------------------------------------------------------------

hd_private& hd_private::operator=(hd_private x) {
    swap(*this, x);
    return *this;
}

bool hd_private::operator<(hd_private const& x) const {
    return encoded() < x.encoded();
}

bool hd_private::operator==(hd_private const& x) const {
    return secret_ == x.secret_ && valid_ == x.valid_ &&
        chain_ == x.chain_ && lineage_ == x.lineage_ &&
        point_ == x.point_;
}

bool hd_private::operator!=(hd_private const& x) const {
    return !(*this == x);
}

// We must assume mainnet for public version here.
// When converting this to public a clone of this key should be used, with the
// public version specified - after validating the private version.
std::istream& operator>>(std::istream& in, hd_private& to) {
    std::string value;
    in >> value;
    to = hd_private(value, hd_public::mainnet);

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

std::ostream& operator<<(std::ostream& out, hd_private const& of) {
    out << of.encoded();
    return out;
}

// friend function, see: stackoverflow.com/a/5695855/1172329
void swap(hd_private& left, hd_private& right) {
    using std::swap;

    // Must be unqualified (no std namespace).
    swap(static_cast<hd_public&>(left), static_cast<hd_public&>(right));
    swap(left.secret_, right.secret_);
}

} // namespace kth::infrastructure::wallet
