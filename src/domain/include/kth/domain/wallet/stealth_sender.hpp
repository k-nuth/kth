// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_SENDER_HPP
#define KTH_WALLET_STEALTH_SENDER_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/// Stealth-address sender. Valid-by-construction: every reachable
/// instance came from `from_stealth_address`, which verifies the
/// derived stealth script and payment address up front; accessors
/// are always meaningful.
///
/// This class does not support multisignature stealth addresses.
struct KD_API stealth_sender {
    /// Generate the send address by drawing a fresh ephemeral private
    /// key from `seed`. Fails when the seed cannot yield a valid
    /// ephemeral key or when any downstream stealth-derivation step
    /// (single spend key, on-curve derivation, script construction,
    /// address wrap) rejects the input.
    [[nodiscard]]
    static
    expect<stealth_sender> from_stealth_address(stealth_address const& address,
                                                data_chunk const& seed,
                                                binary const& filter,
                                                uint8_t version);

    /// Same, using a caller-supplied ephemeral private key.
    [[nodiscard]]
    static
    expect<stealth_sender> from_ephemeral(ec_secret const& ephemeral_private,
                                          stealth_address const& address,
                                          data_chunk const& seed,
                                          binary const& filter,
                                          uint8_t version);

    /// Attach this script to the output before the send output.
    [[nodiscard]]
    chain::script const& stealth_script() const noexcept;

    /// The bitcoin payment address to which the payment will be made.
    [[nodiscard]]
    wallet::payment_address const& payment_address() const noexcept;

private:
    stealth_sender(uint8_t version,
                   chain::script script,
                   wallet::payment_address address);

    uint8_t const version_;
    chain::script const script_;
    wallet::payment_address const address_;
};

} // namespace kth::domain::wallet

#endif
