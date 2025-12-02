// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/ec_private.hpp>

#include <cstdint>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain/wallet/ec_public.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

uint8_t const ec_private::compressed_sentinel = 0x01;
#if defined(KTH_CURRENCY_LTC)
uint8_t const ec_private::mainnet_wif = 0xb0;
uint8_t const ec_private::mainnet_p2kh = 0x30;
#else   //KTH_CURRENCY_LTC
uint8_t const ec_private::mainnet_wif = 0x80;
uint8_t const ec_private::mainnet_p2kh = 0x00;
#endif  //KTH_CURRENCY_LTC

uint16_t const ec_private::mainnet = to_version(mainnet_p2kh, mainnet_wif);

uint8_t const ec_private::testnet_wif = 0xef;
uint8_t const ec_private::testnet_p2kh = 0x6f;
uint16_t const ec_private::testnet = to_version(testnet_p2kh, testnet_wif);

ec_private::ec_private(std::string const& wif, uint8_t version)
    : ec_private(from_string(wif, version))
{}

ec_private::ec_private(wif_compressed const& wif, uint8_t version)
    : ec_private(from_compressed(wif, version))
{}

ec_private::ec_private(wif_uncompressed const& wif, uint8_t version)
    : ec_private(from_uncompressed(wif, version))
{}

ec_private::ec_private(ec_secret const& secret, uint16_t version, bool compress)
    : valid_(true), compress_(compress), version_(version), secret_(secret)
{}

// Validators.
// ----------------------------------------------------------------------------

bool ec_private::is_wif(byte_span decoded) {
    auto const size = decoded.size();
    if (size != wif_compressed_size && size != wif_uncompressed_size) {
        return false;
    }

    if ( ! verify_checksum(decoded)) {
        return false;
    }

    return (size == wif_uncompressed_size) ||
           decoded.data()[1 + ec_secret_size] == compressed_sentinel;
}

// Factories.
// ----------------------------------------------------------------------------

ec_private ec_private::from_string(std::string const& wif, uint8_t version) {
    data_chunk decoded;
    if ( ! decode_base58(decoded, wif) || !is_wif(decoded)) {
        return ec_private();
    }

    auto const compressed = decoded.size() == wif_compressed_size;
    return compressed ? ec_private(to_array<wif_compressed_size>(decoded), version) : ec_private(to_array<wif_uncompressed_size>(decoded), version);
}

ec_private ec_private::from_compressed(wif_compressed const& wif, uint8_t address_version) {
    if ( ! is_wif(wif)) {
        return ec_private();
    }

    uint16_t const version = to_version(address_version, wif.front());
    auto const secret = slice<1, ec_secret_size + 1>(wif);
    return ec_private(secret, version, true);
}

ec_private ec_private::from_uncompressed(wif_uncompressed const& wif, uint8_t address_version) {
    if ( ! is_wif(wif)) {
        return ec_private();
    }

    uint16_t const version = to_version(address_version, wif.front());
    auto const secret = slice<1, ec_secret_size + 1>(wif);
    return ec_private(secret, version, false);
}

// Cast operators.
// ----------------------------------------------------------------------------

ec_private::operator bool() const {
    return valid_;
}

ec_private::operator ec_secret const&() const {
    return secret_;
}

// Serializer.
// ----------------------------------------------------------------------------

// Conversion to WIF loses payment address version info.
std::string ec_private::encoded() const {
    if (compressed()) {
        wif_compressed wif;
        auto const prefix = to_array(wif_version());
        auto const compressed = to_array(compressed_sentinel);
        build_checked_array(wif, {prefix, secret_, compressed});
        return encode_base58(wif);
    }

    wif_uncompressed wif;
    auto const prefix = to_array(wif_version());
    build_checked_array(wif, {prefix, secret_});
    return encode_base58(wif);
}

// Accessors.
// ----------------------------------------------------------------------------

ec_secret const& ec_private::secret() const {
    return secret_;
}

uint16_t ec_private::version() const {
    return version_;
}

uint8_t ec_private::payment_version() const {
    return to_address_prefix(version_);
}

uint8_t ec_private::wif_version() const {
    return to_wif_prefix(version_);
}

bool ec_private::compressed() const {
    return compress_;
}

// Methods.
// ----------------------------------------------------------------------------

// Conversion to ec_public loses all version information.
// In the case of failure the key is always compressed (ec_compressed_null).
ec_public ec_private::to_public() const {
    ec_compressed point;
    return valid_ && secret_to_public(point, secret_) ? ec_public(point, compressed()) : ec_public();
}

payment_address ec_private::to_payment_address() const {
    return payment_address{*this};
}

// Operators.
// ----------------------------------------------------------------------------


bool ec_private::operator<(ec_private const& x) const {
    return encoded() < x.encoded();
}

bool ec_private::operator==(ec_private const& x) const {
    return valid_ == x.valid_ && compress_ == x.compress_ &&
           version_ == x.version_ && secret_ == x.secret_;
}

bool ec_private::operator!=(ec_private const& x) const {
    return !(*this == x);
}

std::istream& operator>>(std::istream& in, ec_private& to) {
    std::string value;
    in >> value;
    to = ec_private(value);

    if ( ! to) {
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
    }

    return in;
}

std::ostream& operator<<(std::ostream& out, ec_private const& of) {
    out << of.encoded();
    return out;
}

} // namespace kth::domain::wallet
