// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_SENDER_HPP
#define KTH_WALLET_STEALTH_SENDER_HPP

#include <cstddef>
#include <cstdint>
#include <utility>

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

    /// Package an already-derived `(script, address)` pair into a
    /// `stealth_sender`. Caller is responsible for the invariants
    /// (`script` is the stealth OP_RETURN with the ephemeral public
    /// key, `address` derives from the receiver's spend key + the
    /// ephemeral private key); no checks are performed here.
    [[nodiscard]] static constexpr
    stealth_sender from_verified_parts(uint8_t version,
                                       chain::script script,
                                       wallet::payment_address address) noexcept {
        return stealth_sender(version, std::move(script), std::move(address));
    }

    /// Attach this script to the output before the send output.
    [[nodiscard]] constexpr
    chain::script const& stealth_script() const noexcept { return script_; }

    /// The bitcoin payment address to which the payment will be made.
    [[nodiscard]] constexpr
    wallet::payment_address const& payment_address() const noexcept { return address_; }

private:
    constexpr
    stealth_sender(uint8_t version,
                   chain::script script,
                   wallet::payment_address address) noexcept
        : version_(version)
        , script_(std::move(script))
        , address_(std::move(address))
    {}

    uint8_t const version_;
    chain::script const script_;
    wallet::payment_address const address_;
};

} // namespace kth::domain::wallet

#endif
