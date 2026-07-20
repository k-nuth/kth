// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_EC_PUBLIC_HPP
#define KTH_DOMAIN_WALLET_EC_PUBLIC_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

class ec_private;
class payment_address;

/// Elliptic-curve public key, either compressed or uncompressed on the
/// wire. Valid-by-construction: every reachable instance came from a
/// factory that validated its input, so accessors and serializers are
/// always meaningful.
struct KD_API ec_public {
    static constexpr uint8_t compressed_even = 0x02;
    static constexpr uint8_t compressed_odd  = 0x03;
    static constexpr uint8_t uncompressed    = 0x04;
#if defined(KTH_CURRENCY_LTC)
    static constexpr uint8_t mainnet_p2kh    = 0x30;
#else
    static constexpr uint8_t mainnet_p2kh    = 0x00;
#endif

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
    ec_public from_private(ec_private const& secret);

    /// Wrap an uncompressed EC point, keeping the compressed/uncompressed
    /// wire form as requested.
    [[nodiscard]]
    static
    expect<ec_public> from_point(ec_uncompressed const& point, bool compress);

    /// Wrap an already-compressed EC point that the caller has verified
    /// lies on the curve (or produced by an internal derivation step
    /// that guarantees it). No on-curve check is performed here.
    [[nodiscard]] static constexpr
    ec_public from_verified_point(ec_compressed const& point, bool compress) noexcept {
        return ec_public(point, compress);
    }

    [[nodiscard]]
    friend auto operator<=>(ec_public const&, ec_public const&) = default;

    [[nodiscard]] constexpr
    ec_compressed const& value() const noexcept { return point_; }

    [[nodiscard]] constexpr
    ec_compressed const& point() const noexcept { return point_; }

    [[nodiscard]] constexpr
    bool compressed() const noexcept { return compress_; }

    /// Base16 encoding used by `fmt::formatter<ec_public>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    std::string encoded() const { return to_string(); }

    [[nodiscard]]
    data_chunk to_data() const;

    [[nodiscard]]
    ec_uncompressed to_uncompressed() const;

    [[nodiscard]]
    expect<payment_address> to_payment_address(uint8_t version = mainnet_p2kh) const;

private:
    constexpr
    ec_public(ec_compressed const& point, bool compress) noexcept
        : compress_(compress), point_(point) {}

    static
    bool is_point(byte_span decoded);

    bool compress_{true};
    ec_compressed point_ = null_compressed_point;
};

} // namespace kth::domain::wallet

#endif
