// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_SENDER_HPP
#define KTH_WALLET_STEALTH_SENDER_HPP

#include <cstdint>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/// This class does not support multisignature stealth addresses.
struct KD_API stealth_sender {
    /// Constructors.
    /// Generate a send address from the stealth address.
    stealth_sender(stealth_address const& address, data_chunk const& seed, binary const& filter, uint8_t version = payment_address::mainnet_p2kh);

    /// Generate a send address from the stealth address.
    stealth_sender(ec_secret const& ephemeral_private,
                   stealth_address const& address,
                   data_chunk const& seed,
                   binary const& filter,
                   uint8_t version = payment_address::mainnet_p2kh);

    /// Caller must test after construct.
    operator bool() const;

    /// Attach this script to the output before the send output.
    [[nodiscard]]
    chain::script const& stealth_script() const;

    /// The bitcoin payment address to which the payment will be made.
    [[nodiscard]]
    const wallet::payment_address& payment_address() const;

private:
    void initialize(ec_secret const& ephemeral_private,
                    stealth_address const& address,
                    data_chunk const& seed,
                    binary const& filter);

    uint8_t const version_;
    chain::script script_;
    wallet::payment_address address_;
};

} // namespace kth::domain::wallet

#endif
