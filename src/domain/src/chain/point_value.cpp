// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdint>
#include <utility>
#include <vector>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/point_value.hpp>

namespace kth::domain::chain {

// Constructors.
//-------------------------------------------------------------------------

point_value::point_value(point const& p, uint64_t value)
    : point(p), value_(value) {
}

// Operators.
//-------------------------------------------------------------------------

// Copy and swap idiom, see: stackoverflow.com/a/3279550/1172329
// point_value& point_value::operator=(point_value x) {
//     swap(*this, x);
//     return *this;
// }

// friend
void swap(point_value& x, point_value& y) {
    using std::swap;
    swap(static_cast<point&>(x), static_cast<point&>(y));
    swap(x.value_, y.value_);
}

// Properties (accessors).
//-------------------------------------------------------------------------

uint64_t point_value::value() const {
    return value_;
}

void point_value::set_value(uint64_t value) {
    value_ = value;
}

} // namespace kth::domain::chain
