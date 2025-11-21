// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_RECEIVER_HPP
#define KTH_WALLET_STEALTH_RECEIVER_HPP

#include <cstdint>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::domain::wallet {

/// This class does not support multisignature stealth addresses.
struct KD_API stealth_receiver {
    /// Constructors.
    stealth_receiver(ec_secret const& scan_private,
                     ec_secret const& spend_private,
                     binary const& filter,
                     uint8_t version = payment_address::mainnet_p2kh);

    /// Caller must test after construct.
    operator bool() const;

    /// Get the stealth address.
    [[nodiscard]]
    const wallet::stealth_address& stealth_address() const;

    /// Derive a payment address to compare against the blockchain.
    bool derive_address(payment_address& out_address,
                        ec_compressed const& ephemeral_public) const;

    /// Once address is discovered, derive the private spend key.
    bool derive_private(ec_secret& out_private,
                        ec_compressed const& ephemeral_public) const;

private:
    uint8_t const version_;
    ec_secret const scan_private_;
    ec_secret const spend_private_;
    ec_compressed spend_public_;
    wallet::stealth_address address_;
};

} // namespace kth::domain::wallet

#endif
