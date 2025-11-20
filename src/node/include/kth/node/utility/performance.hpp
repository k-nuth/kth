// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PERFORMANCE_HPP
#define KTH_NODE_PERFORMANCE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <kth/blockchain.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

class KND_API performance {
public:

    /// The normalized rate derived from the performance values.
    double normal() const;

    /// The rate derived from the performance values (inclusive of store cost).
    double total() const;

    /// The ratio of database time to total time.
    double ratio() const;

    bool idle;
    size_t events;
    uint64_t database;
    uint64_t window;
};

// Coerce division into double and error into zero.
template<typename Quotient, typename Dividend, typename Divisor>
static
Quotient divide(Dividend dividend, Divisor divisor) {
    auto const quotient = static_cast<Quotient>(dividend) / divisor;
    return std::isnan(quotient) || std::isinf(quotient) ? 0.0 : quotient;
}

} // namespace kth::node

#endif

