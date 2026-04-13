// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_HD_LINEAGE_HPP_
#define KTH_DOMAIN_WALLET_HD_LINEAGE_HPP_

#include <compare>
#include <cstdint>

#include <kth/domain/define.hpp>

namespace kth::domain::wallet {

/// Key derivation information used in the serialization format.
struct KD_API hd_lineage {
    uint64_t prefixes;
    uint8_t depth;
    uint32_t parent_fingerprint;
    uint32_t child_number;

    auto operator<=>(hd_lineage const&) const = default;
};

} // namespace kth::domain::wallet

#endif // KTH_DOMAIN_WALLET_HD_LINEAGE_HPP_
