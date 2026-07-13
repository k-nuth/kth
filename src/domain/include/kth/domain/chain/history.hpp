// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HISTORY_HPP
#define KTH_DOMAIN_CHAIN_HISTORY_HPP

#include <cstdint>
#include <vector>

#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::chain {

/// Use "kind" for union differentiation.
enum class point_kind : uint32_t {
    output = 0,
    spend = 1
};

/// This structure models the client-server protocol in v1/v2/v3.
struct KD_API history_compact {
    using list = std::vector<history_compact>;

    // The type of point (output or spend).
    point_kind kind;

    /// The point that identifies the record.
    chain::point point;

    /// The height of the point.
    uint32_t height;

    union {
        /// If output, then satoshi value of output.
        uint64_t value;

        /// If spend, then checksum hash of previous output point
        /// To match up this row with the output, recompute the
        /// checksum from the output row with spend_checksum(row.point)
        uint64_t previous_checksum;
    };

    // Hand-written rather than `= default`: a defaulted comparison on a
    // class with an anonymous union member is defined as deleted (per
    // [class.compare.default]). Both union alternatives are `uint64_t`
    // occupying the same bytes, so reading either one compares the same
    // value regardless of which is "active". `==` is provided alongside
    // because a user-defined `<=>` does not synthesize it.
    friend
    auto operator<=>(history_compact const& x, history_compact const& y) {
        if (auto c = x.kind <=> y.kind; c != 0) return c;
        if (auto c = x.point <=> y.point; c != 0) return c;
        if (auto c = x.height <=> y.height; c != 0) return c;
        return x.value <=> y.value;
    }

    friend
    bool operator==(history_compact const& x, history_compact const& y) {
        return x.kind == y.kind
            && x.point == y.point
            && x.height == y.height
            && x.value == y.value;
    }
};

/// This structure is used between client and API callers in v3.
/// This structure models the client-server protocol in v1/v2.
/// The height values here are 64 bit, but 32 bits on the wire.
struct KD_API history {
    using list = std::vector<history>;

    /// If there is no output this is null_hash:max.
    output_point output;
    uint64_t output_height;

    /// The satoshi value of the output.
    uint64_t value;

    /// If there is no spend this is null_hash:max.
    input_point spend;

    union {
        /// The height of the spend or max if no spend.
        uint64_t spend_height;

        /// During expansion this value temporarily doubles as a checksum.
        uint64_t temporary_checksum;
    };

    // See the note on `history_compact` — hand-written for the same
    // reason (anonymous union deletes the defaulted comparison). Both
    // union alternatives are `uint64_t`.
    friend
    auto operator<=>(history const& x, history const& y) {
        if (auto c = x.output <=> y.output; c != 0) return c;
        if (auto c = x.output_height <=> y.output_height; c != 0) return c;
        if (auto c = x.value <=> y.value; c != 0) return c;
        if (auto c = x.spend <=> y.spend; c != 0) return c;
        return x.spend_height <=> y.spend_height;
    }

    friend
    bool operator==(history const& x, history const& y) {
        return x.output == y.output
            && x.output_height == y.output_height
            && x.value == y.value
            && x.spend == y.spend
            && x.spend_height == y.spend_height;
    }
};

} // namespace kth::domain::chain

#endif
