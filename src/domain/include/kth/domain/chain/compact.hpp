// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_COMPACT_HPP
#define KTH_DOMAIN_CHAIN_COMPACT_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::chain {

/// A signed but zero-floored scientific notation in 32 bits.
///
/// Valid-by-construction: an existing `compact` always holds a target that
/// fits in 256 bits. The fallible path (parsing an untrusted 32-bit compact
/// number that may overflow) is `from_compact`, which returns
/// `error::overflow` instead of a silently-flagged object.
struct KD_API compact {
    /// Construct a normal form compact number from a 256 bit number.
    /// Always valid: every 256-bit value has a representable compact form.
    explicit
    compact(uint256_t const& big);

    /// Parse a 32 bit compact number. Returns `error::overflow` when the
    /// exponent would shift the mantissa past 32 bytes (an invalid target
    /// encoding). Negative encodings are consensus-floored to zero, not an
    /// error.
    static
    expect<compact> from_compact(uint32_t compact);

    /// Consensus-normalized compact number value.
    /// This is derived from the construction parameter.
    [[nodiscard]]
    uint32_t normal() const;

    /// Big number that the compact number represents.
    operator uint256_t const&() const;

    [[nodiscard]]
    uint256_t const& big() const;

private:
    // Expands a 32-bit compact into a big number. Returns false on overflow
    // (negatives are floored to zero and reported as success).
    static
    bool expand(uint256_t& out, uint32_t compact);

    static
    uint32_t from_big(uint256_t const& big);

    uint256_t big_;
    uint32_t normal_;
};

} // namespace kth::domain::chain

#endif
