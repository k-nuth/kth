// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_RECEIVER_HPP
#define KTH_WALLET_STEALTH_RECEIVER_HPP

#include <cstddef>
#include <cstdint>
#include <utility>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::domain::wallet {

/// Stealth-address receiver. Valid-by-construction: every reachable
/// instance came from `from_secrets`, which verifies both private
/// keys lift to points and the resulting `stealth_address` is well-
/// formed; accessors and derivations are always meaningful.
///
/// This class does not support multisignature stealth addresses.
struct KD_API stealth_receiver {
    /// Build a receiver from a scan / spend private-key pair against
    /// a caller-supplied bloom filter and payment-address version.
    /// Fails if either private key is off-curve or if the derived
    /// stealth address is malformed.
    [[nodiscard]]
    static
    expect<stealth_receiver> from_secrets(ec_secret const& scan_private,
                                          ec_secret const& spend_private,
                                          binary const& filter,
                                          uint8_t version);

    /// Package an already-validated tuple into a `stealth_receiver`.
    /// Caller is responsible for the invariants (both privates on
    /// the curve, `spend_public = secret_to_public(spend_private)`,
    /// `address` derives from `scan_private` + `spend_private`); no
    /// checks are performed here.
    [[nodiscard]] static constexpr
    stealth_receiver from_verified_parts(uint8_t version,
                                         ec_secret const& scan_private,
                                         ec_secret const& spend_private,
                                         ec_compressed const& spend_public,
                                         wallet::stealth_address address) noexcept {
        return stealth_receiver(version, scan_private, spend_private,
                                spend_public, std::move(address));
    }

    /// Peer stealth address for this receiver.
    [[nodiscard]] constexpr
    wallet::stealth_address const& stealth_address() const noexcept { return address_; }

    /// Derive a payment address to compare against the blockchain.
    [[nodiscard]]
    expect<payment_address> derive_address(ec_compressed const& ephemeral_public) const;

    /// Once the address is discovered, derive the private spend key
    /// for the corresponding output.
    [[nodiscard]]
    expect<ec_secret> derive_private(ec_compressed const& ephemeral_public) const;

private:
    constexpr
    stealth_receiver(uint8_t version,
                     ec_secret const& scan_private,
                     ec_secret const& spend_private,
                     ec_compressed const& spend_public,
                     wallet::stealth_address address) noexcept
        : version_(version)
        , scan_private_(scan_private)
        , spend_private_(spend_private)
        , spend_public_(spend_public)
        , address_(std::move(address))
    {}

    uint8_t const version_;
    ec_secret const scan_private_;
    ec_secret const spend_private_;
    ec_compressed const spend_public_;
    wallet::stealth_address const address_;
};

} // namespace kth::domain::wallet

#endif
