// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/stealth_sender.hpp>

#include <cstdint>
#include <utility>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

// static
expect<stealth_sender> stealth_sender::from_stealth_address(stealth_address const& address,
                                                            data_chunk const& seed,
                                                            binary const& filter,
                                                            uint8_t version) {
    ec_secret ephemeral_private;
    if ( ! create_ephemeral_key(ephemeral_private, seed)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return from_ephemeral(ephemeral_private, address, seed, filter, version);
}

// static
expect<stealth_sender> stealth_sender::from_ephemeral(ec_secret const& ephemeral_private,
                                                     stealth_address const& address,
                                                     data_chunk const& seed,
                                                     binary const& filter,
                                                     uint8_t version) {
    ec_compressed ephemeral_public;
    if ( ! secret_to_public(ephemeral_public, ephemeral_private)) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const& spend_keys = address.spend_keys();
    if (spend_keys.size() != 1) {
        return std::unexpected(kth::error::illegal_value);
    }

    ec_compressed sender_public;
    if ( ! uncover_stealth(sender_public, address.scan_key(), ephemeral_private, spend_keys.front())) {
        return std::unexpected(kth::error::illegal_value);
    }

    chain::script script;
    if ( ! create_stealth_script(script, ephemeral_private, filter, seed)) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto payment = wallet::payment_address::from_ec_public(
        ec_public::from_verified_point(sender_public, true), version);
    if ( ! payment) {
        return std::unexpected(payment.error());
    }

    return stealth_sender(version, std::move(script), std::move(*payment));
}

} // namespace kth::domain::wallet
