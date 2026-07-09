// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/ec_public.hpp>

#include <string>
#include <string_view>

#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

uint8_t const ec_public::compressed_even = 0x02;
uint8_t const ec_public::compressed_odd  = 0x03;
uint8_t const ec_public::uncompressed    = 0x04;
#if defined(KTH_CURRENCY_LTC)
uint8_t const ec_public::mainnet_p2kh = 0x30;
#else
uint8_t const ec_public::mainnet_p2kh = 0x00;
#endif

// Validators.
// ----------------------------------------------------------------------------

bool ec_public::is_point(byte_span decoded) {
    return kth::is_public_key(decoded);
}

// Factories.
// ----------------------------------------------------------------------------

// static
expect<ec_public> ec_public::from_data(data_chunk const& decoded) {
    if ( ! is_point(decoded)) {
        return std::unexpected(kth::error::illegal_value);
    }

    if (decoded.size() == ec_compressed_size) {
        return ec_public{to_array<ec_compressed_size>(decoded), true};
    }

    ec_compressed compressed;
    if ( ! kth::compress(compressed, to_array<ec_uncompressed_size>(decoded))) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_public{compressed, false};
}

// static
expect<ec_public> ec_public::from_private(ec_private const& secret) {
    if ( ! secret.valid()) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_public{secret.to_public()};
}

// static
expect<ec_public> ec_public::from_point(ec_uncompressed const& point, bool compress) {
    if ( ! is_point(point)) {
        return std::unexpected(kth::error::illegal_value);
    }

    ec_compressed compressed;
    if ( ! kth::compress(compressed, point)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_public{compressed, compress};
}

// static
expect<ec_public> ec_public::parse_from(std::string_view base16) {
    auto decoded = decode_base16(base16);
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_data(*decoded);
}

// Serializer.
// ----------------------------------------------------------------------------

std::string ec_public::to_string() const {
    if (compressed()) {
        return encode_base16(point_);
    }

    // A well-formed point always decompresses.
    auto const uncompressed = to_uncompressed();
    return encode_base16(uncompressed ? *uncompressed : null_uncompressed_point);
}

// Methods.
// ----------------------------------------------------------------------------

std::expected<data_chunk, std::error_code> ec_public::to_data() const {
    if ( ! valid_) {
        return std::unexpected(error::pubkey_type);
    }
    if (compressed()) {
        return data_chunk(point_.begin(), point_.begin() + ec_compressed_size);
    }

    auto const uncompressed = to_uncompressed();
    if ( ! uncompressed) {
        return std::unexpected(uncompressed.error());
    }
    return data_chunk(uncompressed->begin(), uncompressed->end());
}

std::expected<ec_uncompressed, std::error_code> ec_public::to_uncompressed() const {
    if ( ! valid_) {
        return std::unexpected(error::pubkey_type);
    }
    ec_uncompressed out;
    if ( ! kth::decompress(out, to_array<ec_compressed_size>(point_))) {
        return std::unexpected(error::pubkey_type);
    }
    return out;
}

expect<payment_address> ec_public::to_payment_address(uint8_t version) const {
    return payment_address::from_ec_public(*this, version);
}

} // namespace kth::domain::wallet
