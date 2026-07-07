// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/stealth_sender.hpp>

#include <cstdint>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

stealth_sender::stealth_sender(stealth_address const& address,
                               data_chunk const& seed,
                               binary const& filter,
                               uint8_t version)
    : version_(version) {
    ec_secret ephemeral_private;
    if (create_ephemeral_key(ephemeral_private, seed)) {
        initialize(ephemeral_private, address, seed, filter);
    }
}

stealth_sender::stealth_sender(ec_secret const& ephemeral_private,
                               stealth_address const& address,
                               data_chunk const& seed,
                               binary const& filter,
                               uint8_t version)
    : version_(version) {
    initialize(ephemeral_private, address, seed, filter);
}

stealth_sender::operator bool() const {
    return address_.valid();
}

// private
// TODO(legacy): convert to factory and make script_ and address_ const.
void stealth_sender::initialize(ec_secret const& ephemeral_private,
                                stealth_address const& address,
                                data_chunk const& seed,
                                binary const& filter) {
    ec_compressed ephemeral_public;
    if ( ! secret_to_public(ephemeral_public, ephemeral_private)) {
        return;
    }

    auto const& spend_keys = address.spend_keys();
    if (spend_keys.size() != 1) {
        return;
    }

    ec_compressed sender_public;
    if ( ! uncover_stealth(sender_public, address.scan_key(), ephemeral_private, spend_keys.front())) {
        return;
    }

    if (create_stealth_script(script_, ephemeral_private, filter, seed)) {
        auto address_result = wallet::payment_address::from_ec_public(ec_public{sender_public}, version_);
        if (address_result) {
            address_ = *address_result;
        }
    }
}

// Will be invalid if construct fails.
chain::script const& stealth_sender::stealth_script() const {
    return script_;
}

// Will be invalid if construct fails.
const wallet::payment_address& stealth_sender::payment_address() const {
    return address_;
}

} // namespace kth::domain::wallet
