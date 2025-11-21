// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_POINTS_VALUE_HPP
#define KTH_DOMAIN_CHAIN_POINTS_VALUE_HPP

#include <cstdint>
#include <numeric>

#include <kth/domain/chain/point_value.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::domain::chain {

struct KD_API points_value {
    /// A set of valued points.
    point_value::list points;

    /// Total value of the current set of points.
    [[nodiscard]]
    uint64_t value() const;
};

} // namespace kth::domain::chain

#endif
