// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_POINT_VALUE_HPP
#define KTH_DOMAIN_CHAIN_POINT_VALUE_HPP

#include <cstdint>
#include <vector>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::chain {

/// A valued point, does not implement specialized serialization methods.
struct KD_API point_value : point {
public:
    using list = std::vector<point_value>;

    // Constructors.
    //-------------------------------------------------------------------------

    constexpr point_value(point const& p, uint64_t value)
        : point(p), value_(value)
    {}

    // Operators.
    //-------------------------------------------------------------------------

    friend
    auto operator<=>(point_value const&, point_value const&) = default;

    // Properties (accessors).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    constexpr uint64_t value() const noexcept {
        return value_;
    }

private:
    uint64_t value_;
};

} // namespace kth::domain::chain

#endif
