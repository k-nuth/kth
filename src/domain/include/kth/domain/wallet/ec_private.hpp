// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_EC_PRIVATE_HPP
#define KTH_DOMAIN_WALLET_EC_PRIVATE_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::wallet {

class payment_address;

/// Private keys with public key compression metadata:
static constexpr
size_t wif_uncompressed_size = 37U;

using wif_uncompressed = byte_array<wif_uncompressed_size>;

static constexpr
size_t wif_compressed_size = wif_uncompressed_size + 1U;

using wif_compressed = byte_array<wif_compressed_size>;

/// EC secret with WIF version and compression metadata.
/// Valid-by-construction: every reachable instance came from a factory
/// that validated the underlying `ec_secret` lies in the curve's
/// scalar range, so `to_public()` never fails.
struct KD_API ec_private {
    static uint8_t const compressed_sentinel;

    // WIF carries a compression flag for payment address generation but
    // assumes a mapping to payment address version. This is insufficient
    // as a parameterized mapping is required, so we use the same technique
    // as with hd keys, merging the two necessary values into one version.
    static uint8_t  const mainnet_wif;
    static uint8_t  const mainnet_p2kh;
    static uint16_t const mainnet;
    static uint8_t  const testnet_wif;
    static uint8_t  const testnet_p2kh;
    static uint16_t const testnet;

    static
    uint8_t to_address_prefix(uint16_t version) {
        return version & 0x00FF;
    }

    static
    uint8_t to_wif_prefix(uint16_t version) {
        return version >> 8;
    }

    // Unfortunately can't use this below to define mainnet (MSVC).
    static
    uint16_t to_version(uint8_t address, uint8_t wif) {
        return uint16_t(wif) << 8 | address;
    }

    /// Parse a WIF-encoded private key. `error::illegal_value` on
    /// malformed input or a secret outside the curve's scalar range.
    [[nodiscard]]
    static
    expect<ec_private> parse_from(std::string_view wif, uint8_t version);

    [[nodiscard]]
    static
    expect<ec_private> from_compressed(wif_compressed const& wif, uint8_t address_version);

    [[nodiscard]]
    static
    expect<ec_private> from_uncompressed(wif_uncompressed const& wif, uint8_t address_version);

    /// Validate `secret` lies in the curve's scalar range and wrap it.
    [[nodiscard]]
    static
    expect<ec_private> from_secret(ec_secret const& secret, uint16_t version, bool compress);

    /// Wrap a secret that the caller has already verified lies in the
    /// curve's scalar range (or produced by an internal derivation step
    /// that guarantees it). No range check is performed here.
    [[nodiscard]]
    static
    ec_private from_verified_secret(ec_secret const& secret, uint16_t version, bool compress);

    [[nodiscard]]
    friend auto operator<=>(ec_private const&, ec_private const&) = default;

    [[nodiscard]]
    ec_secret const& value() const noexcept { return secret_; }

    [[nodiscard]]
    ec_secret const& secret() const noexcept { return secret_; }

    [[nodiscard]]
    uint16_t version() const noexcept { return version_; }

    [[nodiscard]]
    uint8_t payment_version() const noexcept { return to_address_prefix(version_); }

    [[nodiscard]]
    uint8_t wif_version() const noexcept { return to_wif_prefix(version_); }

    [[nodiscard]]
    bool compressed() const noexcept { return compress_; }

    /// WIF encoding used by `fmt::formatter<ec_private>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    ec_public to_public() const;

    [[nodiscard]]
    expect<payment_address> to_payment_address() const;

private:
    ec_private(ec_secret const& secret, uint16_t version, bool compress) noexcept
        : compress_(compress), version_(version), secret_(secret) {}

    static
    bool is_wif(byte_span decoded);

    bool      compress_{true};
    uint16_t  version_{0};
    ec_secret secret_{null_hash};
};

} // namespace kth::domain::wallet

#endif
