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

    point_value() = default;
    point_value(point const& p, uint64_t value);

    // point_value(point_value const& x) = default;
    // point_value& operator=(point_value const& x) = default;

    // Operators.
    //-------------------------------------------------------------------------

    friend
    bool operator==(point_value const& x, point_value const& y);

    friend
    bool operator!=(point_value const& x, point_value const& y);

    // Swap implementation required to properly handle base class.
    friend
    void swap(point_value& x, point_value& y);

    // Properties (accessors).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    uint64_t value() const;

    void set_value(uint64_t value);

private:
    uint64_t value_{0};
};

} // namespace kth::domain::chain

#endif
