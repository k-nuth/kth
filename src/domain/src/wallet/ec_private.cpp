// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/ec_private.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

uint8_t const ec_private::compressed_sentinel = 0x01;
#if defined(KTH_CURRENCY_LTC)
uint8_t const ec_private::mainnet_wif  = 0xb0;
uint8_t const ec_private::mainnet_p2kh = 0x30;
#else
uint8_t const ec_private::mainnet_wif  = 0x80;
uint8_t const ec_private::mainnet_p2kh = 0x00;
#endif

uint16_t const ec_private::mainnet = to_version(mainnet_p2kh, mainnet_wif);

uint8_t  const ec_private::testnet_wif  = 0xef;
uint8_t  const ec_private::testnet_p2kh = 0x6f;
uint16_t const ec_private::testnet      = to_version(testnet_p2kh, testnet_wif);

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

// static
expect<ec_private> ec_private::parse_from(std::string_view wif, uint8_t version) {
    data_chunk decoded;
    if ( ! decode_base58(decoded, std::string{wif}) || ! is_wif(decoded)) {
        return std::unexpected(kth::error::illegal_value);
    }

    if (decoded.size() == wif_compressed_size) {
        return from_compressed(to_array<wif_compressed_size>(decoded), version);
    }
    return from_uncompressed(to_array<wif_uncompressed_size>(decoded), version);
}

// static
expect<ec_private> ec_private::from_compressed(wif_compressed const& wif, uint8_t address_version) {
    if ( ! is_wif(wif)) {
        return std::unexpected(kth::error::illegal_value);
    }

    uint16_t const version = to_version(address_version, wif.front());
    auto const secret = slice<1, ec_secret_size + 1>(wif);
    ec_private out{secret, version, true};
    return out;
}

// static
expect<ec_private> ec_private::from_uncompressed(wif_uncompressed const& wif, uint8_t address_version) {
    if ( ! is_wif(wif)) {
        return std::unexpected(kth::error::illegal_value);
    }

    uint16_t const version = to_version(address_version, wif.front());
    auto const secret = slice<1, ec_secret_size + 1>(wif);
    ec_private out{secret, version, false};
    return out;
}

// Serializer.
// ----------------------------------------------------------------------------

// Conversion to WIF loses payment address version info.
std::string ec_private::to_string() const {
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

// Methods.
// ----------------------------------------------------------------------------

// A valid secret always produces a valid public point (the failure paths
// are only reachable for a secret that isn't a valid EC scalar, which
// shouldn't happen for anything constructed via the parse_from factories).
expect<ec_public> ec_private::to_public() const {
    ec_compressed point;
    if ( ! secret_to_public(point, secret_)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_public{point, compressed()};
}

expect<payment_address> ec_private::to_payment_address() const {
    return payment_address::from_ec_private(*this);
}

} // namespace kth::domain::wallet
