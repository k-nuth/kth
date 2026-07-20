// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_POINTS_VALUE_HPP
#define KTH_DOMAIN_CHAIN_POINTS_VALUE_HPP

#include <cstdint>

#include <kth/domain/chain/point_value.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::chain {

struct KD_API points_value {
    /// A set of valued points.
    point_value::list points;

    friend
    auto operator<=>(points_value const&, points_value const&) = default;

    /// Total value of the current set of points.
    // No overflow check: the sum is bounded by BCH's max money supply
    // (21e6 * 1e8 satoshis ≈ 2.1e15) which fits in uint64_t (max 1.8e19)
    // with room to spare, even summing millions of prevouts.
    [[nodiscard]]
    uint64_t value() const {
        uint64_t total = 0;
        for (auto const& p : points) {
            total += p.value();
        }
        return total;
    }
};

} // namespace kth::domain::chain

#endif
