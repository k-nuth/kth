// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/point_iterator.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/utility/assert.hpp>
////#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::domain::chain {

// static auto const point_size = unsigned(std::tuple_size<point>::value);
constexpr auto point_size = unsigned(std::tuple_size<point>::value);

// Constructors.
//-----------------------------------------------------------------------------

point_iterator::point_iterator(point const& value, unsigned index)
    : point_(&value), current_(index)
{}

// Operators.
//-----------------------------------------------------------------------------

point_iterator::operator bool() const {
    return current_ < point_size;
}

// private
uint8_t point_iterator::current() const {
    if (current_ < hash_size) {
        return point_->hash()[current_];
    }

    // TODO(legacy): move the little-endian iterator into endian.hpp.
    auto const position = current_ - hash_size;
    return uint8_t(point_->index() >> (position * byte_bits));
}

point_iterator::pointer point_iterator::operator->() const {
    return current();
}

point_iterator::reference point_iterator::operator*() const {
    return current();
}

point_iterator::iterator& point_iterator::operator++() {
    increment();
    return *this;
}

point_iterator::iterator point_iterator::operator++(int) {
    auto it = *this;
    increment();
    return it;
}

point_iterator::iterator& point_iterator::operator--() {
    decrement();
    return *this;
}

point_iterator::iterator point_iterator::operator--(int) {
    auto it = *this;
    decrement();
    return it;
}

point_iterator point_iterator::operator+(int value) const {
    return value < 0 ? decrease(unsigned(std::abs(value))) : increase(value);
}

point_iterator point_iterator::operator-(int value) const {
    return value < 0 ? increase(unsigned(std::abs(value))) : decrease(value);
}

bool point_iterator::operator==(point_iterator const& x) const {
    return (current_ == x.current_) && (point_ == x.point_);
}

bool point_iterator::operator!=(point_iterator const& x) const {
    return !(*this == x);
}

// Utilities.
//-----------------------------------------------------------------------------
// private

void point_iterator::increment() {
    if (current_ < point_size) {
        current_++;
    }
}

void point_iterator::decrement() {
    if (current_ > 0) {
        current_--;
    }
}

point_iterator point_iterator::increase(unsigned value) const {
    auto const index = ceiling_add(current_, value);
    return {*point_, std::max(index, point_size)};
}

point_iterator point_iterator::decrease(unsigned value) const {
    auto const index = floor_subtract(current_, value);
    return {*point_, index};
}

} // namespace kth::domain::chain
