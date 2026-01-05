// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/utility/performance.hpp>

#include <kth/domain.hpp>

namespace kth::node {

double performance::normal() const {
    // If numerator is small we can overflow (infinity).
    return divide<double>(events, double(window) - database);
}

double performance::total() const {
    return divide<double>(events, window);
}

double performance::ratio() const {
    return divide<double>(database, window);
}

} // namespace kth::node
