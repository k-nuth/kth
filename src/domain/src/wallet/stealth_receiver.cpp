// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/stealth_receiver.hpp>

#include <cstdint>
#include <utility>

#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::domain::wallet {

// static
expect<stealth_receiver> stealth_receiver::from_secrets(ec_secret const& scan_private,
                                                       ec_secret const& spend_private,
                                                       binary const& filter,
                                                       uint8_t version) {
    ec_compressed scan_public;
    if ( ! secret_to_public(scan_public, scan_private)) {
        return std::unexpected(kth::error::illegal_value);
    }

    ec_compressed spend_public;
    if ( ! secret_to_public(spend_public, spend_private)) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto address = stealth_address::from_components(
        filter, scan_public, {spend_public}, 0, stealth_address::mainnet_p2kh);
    if ( ! address) {
        return std::unexpected(address.error());
    }

    return stealth_receiver(version, scan_private, spend_private,
                            spend_public, std::move(*address));
}

expect<payment_address> stealth_receiver::derive_address(ec_compressed const& ephemeral_public) const {
    ec_compressed receiver_public;
    if ( ! uncover_stealth(receiver_public, ephemeral_public, scan_private_, spend_public_)) {
        return std::unexpected(kth::error::illegal_value);
    }

    return payment_address::from_ec_public(
        ec_public::from_verified_point(receiver_public, true), version_);
}

expect<ec_secret> stealth_receiver::derive_private(ec_compressed const& ephemeral_public) const {
    ec_secret out;
    if ( ! uncover_stealth(out, ephemeral_public, scan_private_, spend_private_)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return out;
}

} // namespace kth::domain::wallet
