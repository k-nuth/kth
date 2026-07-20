// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/output_point.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/constants.hpp>

namespace kth::domain::chain {

// Constructors.
//-----------------------------------------------------------------------------

// Default = coinbase null prevout (matches `output_point::null()`).
output_point::output_point()
    : point(null_hash, null_index), validation{}
{}

output_point::output_point(hash_digest const& hash, uint32_t index)
    : point(hash, index), validation{}
{}

output_point::output_point(point const& x)
    : point(x), validation{}
{}

// Operators.
//-----------------------------------------------------------------------------

output_point& output_point::operator=(point const& x) {
    point::operator=(x);
    validation = {};
    return *this;
}

// Validation.
//-----------------------------------------------------------------------------

// For tx pool validation height is that of the candidate block.
bool output_point::is_mature(size_t height) const {
    // Coinbase (null) inputs and those with non-coinbase prevouts are mature.
    if ( ! validation.coinbase || is_null()) {
        return true;
    }

    // The (non-coinbase) input refers to a coinbase output, so validate depth.
    return floor_subtract(height, validation.height) >= coinbase_maturity;
}

} // namespace kth::domain::chain
