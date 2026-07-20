// Copyright (c) 2016-present Knuth Project developers.
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
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

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
        return ec_public(to_array<ec_compressed_size>(decoded), true);
    }

    ec_compressed compressed;
    if ( ! kth::compress(compressed, to_array<ec_uncompressed_size>(decoded))) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_public(compressed, false);
}

// static
ec_public ec_public::from_private(ec_private const& secret) {
    return secret.to_public();
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
    return ec_public(compressed, compress);
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
    return encode_base16(to_uncompressed());
}

// Methods.
// ----------------------------------------------------------------------------

data_chunk ec_public::to_data() const {
    if (compressed()) {
        return data_chunk(point_.begin(), point_.begin() + ec_compressed_size);
    }
    auto const uncompressed = to_uncompressed();
    return data_chunk(uncompressed.begin(), uncompressed.end());
}

ec_uncompressed ec_public::to_uncompressed() const {
    // A valid compressed point always decompresses; the factories that
    // produce `ec_public` all validate on-curve first. Assert catches
    // the only invariant-breaking path: `from_verified_point` called
    // with an off-curve point.
    ec_uncompressed out;
    DEBUG_ONLY(auto const ok =) kth::decompress(out, to_array<ec_compressed_size>(point_));
    KTH_ASSERT_MSG(ok, "decompress failed — from_verified_point called with an off-curve point");
    return out;
}

expect<payment_address> ec_public::to_payment_address(uint8_t version) const {
    return payment_address::from_ec_public(*this, version);
}

} // namespace kth::domain::wallet
