// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_ADDRESS_HPP
#define KTH_WALLET_STEALTH_ADDRESS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/// Stealth payment address. Valid-by-construction: every reachable
/// instance was produced by a factory that validated its inputs.
struct KD_API stealth_address {
    /// DEPRECATED: we intend to make p2kh same as payment address versions.
    static constexpr uint8_t mainnet_p2kh = 0x2a;

    /// If set and the spend_keys contains the scan_key then the key is reused.
    static constexpr uint8_t reuse_key_flag = 1U << 0U;

    /// This is advisory in nature and likely to be enforced by a server.
    static constexpr size_t min_filter_bits = 1 * byte_bits;

    /// This is the protocol limit to the size of a stealth prefix filter.
    static constexpr size_t max_filter_bits = sizeof(uint32_t) * byte_bits;

    [[nodiscard]]
    static
    expect<stealth_address> parse_from(std::string_view encoded);

    /// Parse the wire encoding
    /// (`[version][options][scan_pubkey][N][spend_pubkeys][sigs][filter_bits][filter][checksum]`).
    [[nodiscard]]
    static
    expect<stealth_address> from_data(data_chunk const& decoded);

    /// Wrap a caller-supplied component tuple. Coerces `signatures` to
    /// a valid range and folds `scan_key` into `spend_keys` if the
    /// list is empty (matches the historical constructor's behaviour).
    /// Fails on too-many spend keys (>255) or an over-long filter.
    [[nodiscard]]
    static
    expect<stealth_address> from_components(binary const& filter,
                                            ec_compressed const& scan_key,
                                            point_list const& spend_keys,
                                            uint8_t signatures,
                                            uint8_t version);

    /// Package an already-validated component tuple into a
    /// `stealth_address`. Caller is responsible for the invariants
    /// (signatures in range, spend_keys non-empty, filter within
    /// protocol limits); no coercion or checks are performed here.
    [[nodiscard]] static constexpr
    stealth_address from_verified_components(binary filter,
                                             ec_compressed const& scan_key,
                                             point_list spend_keys,
                                             uint8_t signatures,
                                             uint8_t version) noexcept {
        return stealth_address(version,
                               std::move(filter),
                               scan_key,
                               std::move(spend_keys),
                               signatures);
    }

    [[nodiscard]]
    friend auto operator<=>(stealth_address const&, stealth_address const&) = default;

    /// Base58 encoding used by `fmt::formatter<stealth_address>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]] constexpr
    uint8_t version() const noexcept { return version_; }

    [[nodiscard]] constexpr
    ec_compressed const& scan_key() const noexcept { return scan_key_; }

    [[nodiscard]] constexpr
    point_list const& spend_keys() const noexcept { return spend_keys_; }

    [[nodiscard]] constexpr
    uint8_t signatures() const noexcept { return signatures_; }

    [[nodiscard]] constexpr
    binary const& filter() const noexcept { return filter_; }

    [[nodiscard]]
    data_chunk to_chunk() const;

private:
    constexpr
    stealth_address(uint8_t version,
                    binary filter,
                    ec_compressed const& scan_key,
                    point_list spend_keys,
                    uint8_t signatures) noexcept
        : version_(version)
        , scan_key_(scan_key)
        , spend_keys_(std::move(spend_keys))
        , signatures_(signatures)
        , filter_(std::move(filter))
    {}

    [[nodiscard]]
    bool reuse_key() const;

    [[nodiscard]]
    uint8_t options() const;

    uint8_t       version_{0};
    ec_compressed scan_key_;
    point_list    spend_keys_;
    uint8_t       signatures_{0};
    binary        filter_;
};

} // namespace kth::domain::wallet

#endif
