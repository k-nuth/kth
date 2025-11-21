// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_STEALTH_ADDRESS_HPP
#define KTH_WALLET_STEALTH_ADDRESS_HPP

#include <cstdint>
#include <iostream>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/// A class for working with stealth payment addresses.
struct KD_API stealth_address {
    /// DEPRECATED: we intend to make p2kh same as payment address versions.
    static
    uint8_t const mainnet_p2kh;

    /// If set and the spend_keys contains the scan_key then the key is reused.
    static
    uint8_t const reuse_key_flag;

    /// This is advisory in nature and likely to be enforced by a server.
    static
    size_t const min_filter_bits;

    /// This is the protocol limit to the size of a stealth prefix filter.
    static
    size_t const max_filter_bits;

    /// Constructors.
    stealth_address();
    stealth_address(data_chunk const& decoded);
    stealth_address(std::string const& encoded);
    stealth_address(binary const& filter, ec_compressed const& scan_key, point_list const& spend_keys, uint8_t signatures = 0, uint8_t version = mainnet_p2kh);

    stealth_address(stealth_address const& x) = default;
    stealth_address& operator=(stealth_address const& x) = default;

    /// Operators.
    bool operator==(stealth_address const& x) const;
    bool operator!=(stealth_address const& x) const;
    bool operator<(stealth_address const& x) const;
    friend std::istream& operator>>(std::istream& in, stealth_address& to);
    friend std::ostream& operator<<(std::ostream& out, stealth_address const& of);

    /// Cast operators.
    operator bool() const;
    operator data_chunk() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    uint8_t version() const;

    [[nodiscard]]
    ec_compressed const& scan_key() const;

    [[nodiscard]]
    point_list const& spend_keys() const;

    [[nodiscard]]
    uint8_t signatures() const;

    [[nodiscard]]
    binary const& filter() const;

    /// Methods.
    [[nodiscard]]
    data_chunk to_chunk() const;

private:
    /// Factories.
    static
    stealth_address from_string(std::string const& encoded);

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

    /// Helpers.
    [[nodiscard]]
    bool reuse_key() const;

    [[nodiscard]]
    uint8_t options() const;

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    uint8_t version_{0};
    ec_compressed scan_key_;
    point_list spend_keys_;
    uint8_t signatures_{0};
    binary filter_;
};

} // namespace kth::domain::wallet

#endif
