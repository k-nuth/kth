// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_ADDRESS_HPP
#define KTH_WALLET_STEALTH_ADDRESS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/**
 * Stealth payment address.
 *
 * Default-constructible so callers that hold a `stealth_address` as a
 * struct member (e.g. `stealth_receiver::address_`) can fill it later
 * via assignment. Fallible construction goes through `parse_from`,
 * which returns `expect<stealth_address>`.
 */
struct KD_API stealth_address {
    /// DEPRECATED: we intend to make p2kh same as payment address versions.
    static uint8_t const mainnet_p2kh;

    /// If set and the spend_keys contains the scan_key then the key is reused.
    static uint8_t const reuse_key_flag;

    /// This is advisory in nature and likely to be enforced by a server.
    static size_t const min_filter_bits;

    /// This is the protocol limit to the size of a stealth prefix filter.
    static size_t const max_filter_bits;

    [[nodiscard]]
    static
    expect<stealth_address> parse_from(std::string_view encoded);

    stealth_address();

    explicit
    stealth_address(data_chunk const& decoded);

    stealth_address(binary const& filter, ec_compressed const& scan_key, point_list const& spend_keys, uint8_t signatures = 0, uint8_t version = mainnet_p2kh);

    [[nodiscard]]
    friend bool operator==(stealth_address const&, stealth_address const&) = default;

    [[nodiscard]]
    friend auto operator<=>(stealth_address const& a, stealth_address const& b) {
        return a.encoded() <=> b.encoded();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    /// Base58 encoding used by `fmt::formatter<stealth_address>`.
    [[nodiscard]]
    std::string encoded() const;

    [[nodiscard]]
    std::string to_string() const { return encoded(); }

    [[nodiscard]]
    uint8_t version() const noexcept { return version_; }

    [[nodiscard]]
    ec_compressed const& scan_key() const noexcept { return scan_key_; }

    [[nodiscard]]
    point_list const& spend_keys() const noexcept { return spend_keys_; }

    [[nodiscard]]
    uint8_t signatures() const noexcept { return signatures_; }

    [[nodiscard]]
    binary const& filter() const noexcept { return filter_; }

    [[nodiscard]]
    data_chunk to_chunk() const;

private:
    static
    stealth_address from_stealth(data_chunk const& decoded);

    static
    stealth_address from_stealth(binary const& filter,
                                 ec_compressed const& scan_key,
                                 point_list const& spend_keys,
                                 uint8_t signatures,
                                 uint8_t version);

    /// Parameter order is used to change the constructor signature.
    stealth_address(uint8_t version, binary const& filter, ec_compressed const& scan_key, point_list const& spend_keys, uint8_t signatures);

    [[nodiscard]]
    bool reuse_key() const;

    [[nodiscard]]
    uint8_t options() const;

    // These should be const, apart from the need to implement assignment.
    bool          valid_{false};
    uint8_t       version_{0};
    ec_compressed scan_key_;
    point_list    spend_keys_;
    uint8_t       signatures_{0};
    binary        filter_;
};

} // namespace kth::domain::wallet

#endif
