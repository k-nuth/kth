// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_EC_PUBLIC_HPP
#define KTH_DOMAIN_WALLET_EC_PUBLIC_HPP

#include <cstdint>
#include <iostream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

class ec_private;
class payment_address;

/// Use to pass an ec point as either ec_compressed or ec_uncompressed.
/// ec_public doesn't carry a version for address creation or base58 encoding.
struct KD_API ec_public {
    static
    uint8_t const compressed_even;

    static
    uint8_t const compressed_odd;

    static
    uint8_t const uncompressed;

    static
    uint8_t const mainnet_p2kh;

    /// Constructors.
    ec_public() = default;

    explicit
    ec_public(ec_private const& secret);

    explicit
    ec_public(data_chunk const& decoded);

    explicit
    ec_public(std::string const& base16);

    explicit
    ec_public(ec_compressed const& point, bool compress = true);

    explicit
    ec_public(ec_uncompressed const& point, bool compress = false);

    ec_public(ec_public const& x) = default;
    ec_public& operator=(ec_public const& x) = default;

    /// Operators.
    bool operator==(ec_public const& x) const;
    bool operator!=(ec_public const& x) const;
    bool operator<(ec_public const& x) const;

    friend std::istream& operator>>(std::istream& in, ec_public& to);
    friend std::ostream& operator<<(std::ostream& out, ec_public const& of);

    /// Cast operators.
    operator bool() const;
    operator ec_compressed const&() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    ec_compressed const& point() const;

    [[nodiscard]]
    uint16_t version() const;

    [[nodiscard]]
    uint8_t payment_version() const;

    [[nodiscard]]
    uint8_t wif_version() const;

    [[nodiscard]]
    bool compressed() const;

    /// Methods.
    bool to_data(data_chunk& out) const;
    bool to_uncompressed(ec_uncompressed& out) const;

    [[nodiscard]]
    payment_address to_payment_address(uint8_t version = mainnet_p2kh) const;

private:
    /// Validators.
    static
    bool is_point(data_slice decoded);

    /// Factories.
    static
    ec_public from_data(data_chunk const& decoded);

    static
    ec_public from_private(ec_private const& secret);

    static
    ec_public from_string(std::string const& base16);

    static
    ec_public from_point(ec_uncompressed const& point, bool compress);

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    bool compress_{true};
    uint8_t version_;
    ec_compressed point_ = null_compressed_point;
};

} // namespace kth::domain::wallet

#endif
