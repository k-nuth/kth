// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/stealth_receiver.hpp>

#include <cstdint>

#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::domain::wallet {

// TODO(legacy): use to factory and make address_ and spend_public_ const.
stealth_receiver::stealth_receiver(ec_secret const& scan_private,
                                   ec_secret const& spend_private,
                                   binary const& filter,
                                   uint8_t version)
    : version_(version), scan_private_(scan_private), spend_private_(spend_private) {
    ec_compressed scan_public;
    if (secret_to_public(scan_public, scan_private_) &&
        secret_to_public(spend_public_, spend_private_)) {
        auto built = stealth_address::from_components(
            filter, scan_public, {spend_public_}, 0, stealth_address::mainnet_p2kh);
        if (built) {
            address_ = std::move(*built);
        }
    }
}

stealth_receiver::operator bool() const {
    return address_.has_value();
}

// Precondition: `operator bool()` returned true.
const wallet::stealth_address& stealth_receiver::stealth_address() const {
    return *address_;
}

bool stealth_receiver::derive_address(payment_address& out_address,
                                      ec_compressed const& ephemeral_public) const {
    ec_compressed receiver_public;
    if ( ! uncover_stealth(receiver_public, ephemeral_public, scan_private_,
                         spend_public_)) {
        return false;
    }

    auto result = payment_address::from_ec_public(ec_public::from_verified_point(receiver_public, true), version_);
    if ( ! result) {
        return false;
    }
    out_address = *result;
    return true;
}

bool stealth_receiver::derive_private(ec_secret& out_private,
                                      ec_compressed const& ephemeral_public) const {
    return uncover_stealth(out_private, ephemeral_public, scan_private_,
                           spend_private_);
}

} // namespace kth::domain::wallet
