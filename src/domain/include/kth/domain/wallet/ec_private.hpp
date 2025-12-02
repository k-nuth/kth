// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_EC_PRIVATE_HPP
#define KTH_DOMAIN_WALLET_EC_PRIVATE_HPP

#include <cstdint>
#include <iostream>
#include <string>

#include <kth/domain/define.hpp>
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

/// Use to pass an ec secret with compresson and version information.
struct KD_API ec_private {
    static
    uint8_t const compressed_sentinel;

    // WIF carries a compression flag for payment address generation but
    // assumes a mapping to payment address version. This is insufficient
    // as a parameterized mapping is required, so we use the same technique as
    // with hd keys, merging the two necessary values into one version.
    static
    uint8_t const mainnet_wif;

    static
    uint8_t const mainnet_p2kh;

    static
    uint16_t const mainnet;

    static
    uint8_t const testnet_wif;

    static
    uint8_t const testnet_p2kh;

    static
    uint16_t const testnet;

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

    /// Constructors.
    ec_private() = default;
    
    explicit
    ec_private(std::string const& wif, uint8_t version = mainnet_p2kh);
    
    explicit
    ec_private(wif_compressed const& wif, uint8_t version = mainnet_p2kh);
    
    explicit
    ec_private(const wif_uncompressed& wif, uint8_t version = mainnet_p2kh);

    /// The version is 16 bits. The most significant byte is the WIF prefix and
    /// the least significant byte is the address perfix. 0x8000 by default.
    explicit
    ec_private(ec_secret const& secret, uint16_t version = mainnet, bool compress = true);

    ec_private(ec_private const& x) = default;
    ec_private& operator=(ec_private const& x) = default;

    /// Operators.
    bool operator==(ec_private const& x) const;
    bool operator!=(ec_private const& x) const;
    bool operator<(ec_private const& x) const;

    friend std::istream& operator>>(std::istream& in, ec_private& to);
    friend std::ostream& operator<<(std::ostream& out, ec_private const& of);

    /// Cast operators.
    operator bool() const;
    operator ec_secret const&() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    ec_secret const& secret() const;

    [[nodiscard]]
    uint16_t version() const;

    [[nodiscard]]
    uint8_t payment_version() const;

    [[nodiscard]]
    uint8_t wif_version() const;

    [[nodiscard]]
    bool compressed() const;

    /// Methods.
    [[nodiscard]]
    ec_public to_public() const;

    [[nodiscard]]
    payment_address to_payment_address() const;

private:
    /// Validators.
    static
    bool is_wif(byte_span decoded);

    /// Factories.
    static
    ec_private from_string(std::string const& wif, uint8_t version);

    static
    ec_private from_compressed(wif_compressed const& wif, uint8_t address_version);

    static
    ec_private from_uncompressed(const wif_uncompressed& wif, uint8_t address_version);

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    bool compress_{true};
    uint16_t version_{0};
    ec_secret secret_{null_hash};
};

} // namespace kth::domain::wallet

#endif
