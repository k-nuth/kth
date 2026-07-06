// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_EC_PUBLIC_HPP
#define KTH_DOMAIN_WALLET_EC_PUBLIC_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

class ec_private;
class payment_address;

/**
 * Elliptic-curve public key, either compressed or uncompressed.
 *
 * Default-constructible (invalid state) so the C-API generator can
 * hand out an "empty" handle; the fallible construction paths return
 * `expect<ec_public>`.
 */
struct KD_API ec_public {
    static uint8_t const compressed_even;
    static uint8_t const compressed_odd;
    static uint8_t const uncompressed;
    static uint8_t const mainnet_p2kh;

    /// Parse a base16-encoded key. `error::illegal_value` on malformed
    /// input or a value that fails EC curve verification.
    [[nodiscard]]
    static
    expect<ec_public> parse_from(std::string_view base16);

    /// Wrap a decoded chunk (33 or 65 bytes).
    [[nodiscard]]
    static
    expect<ec_public> from_data(data_chunk const& decoded);

    /// Derive from a validated `ec_private`.
    [[nodiscard]]
    static
    expect<ec_public> from_private(ec_private const& secret);

    /// Wrap an uncompressed EC point, keeping the compressed/uncompressed
    /// wire form as requested.
    [[nodiscard]]
    static
    expect<ec_public> from_point(ec_uncompressed const& point, bool compress);

    ec_public() = default;

    /// Wrap an already-compressed EC point.
    explicit
    ec_public(ec_compressed const& compressed_point, bool compress = true) noexcept
        : valid_(true), compress_(compress), point_(compressed_point) {}

    [[nodiscard]]
    friend bool operator==(ec_public const&, ec_public const&) = default;

    [[nodiscard]]
    friend auto operator<=>(ec_public const& a, ec_public const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    [[nodiscard]]
    ec_compressed const& value() const noexcept { return point_; }

    [[nodiscard]]
    ec_compressed const& point() const noexcept { return point_; }

    [[nodiscard]]
    bool compressed() const noexcept { return compress_; }

    /// Base16 encoding used by `fmt::formatter<ec_public>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    std::string encoded() const { return to_string(); }

    [[nodiscard]]
    std::expected<data_chunk, std::error_code> to_data() const;

    [[nodiscard]]
    std::expected<ec_uncompressed, std::error_code> to_uncompressed() const;

    [[nodiscard]]
    payment_address to_payment_address(uint8_t version = mainnet_p2kh) const;

private:
    static
    bool is_point(byte_span decoded);

    bool valid_{false};
    bool compress_{true};
    ec_compressed point_ = null_compressed_point;
};

} // namespace kth::domain::wallet

#endif
