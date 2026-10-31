// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_FASTER_BLOCKS_HPP
#define KTH_DOMAIN_CHAIN_FASTER_BLOCKS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace kth::domain::chain {

// CHIP-2025-03 activation discovery.
//
// H_a is the first block of a branch whose own median time past reaches the
// activation time. The next block is the first one at the 60-tick interval.
//
// On a valid chain every block's timestamp exceeds the MTP of its parent, which
// makes the MTP non-decreasing along the chain, so "first height with
// MTP >= time" is a monotone predicate and can be bisected instead of walked.
// The lookup is injected so the same search serves the header index, the
// pooled branch and the persisted chain; nothing is cached between calls.

inline constexpr size_t faster_blocks_mtp_span = 11;

// Median (modulo-2 selection, as consensus defines it) of the up to 11
// timestamps ending at `height`. `timestamp_at(height)` yields nullopt when the
// height cannot be read.
template <typename TimestampAt>
std::optional<uint32_t> median_time_past_at(size_t height, TimestampAt&& timestamp_at) {
    std::array<uint32_t, faster_blocks_mtp_span> window{};
    size_t const count = std::min(height + 1, faster_blocks_mtp_span);
    for (size_t i = 0; i < count; ++i) {
        auto const ts = timestamp_at(height - i);
        if ( ! ts) {
            return std::nullopt;
        }
        window[i] = *ts;
    }
    std::sort(window.begin(), window.begin() + count);
    return window[count / 2];
}

// Precondition (checked): the MTP at `parent_height` reaches `activation_time`.
// Returns nullopt when it does not, or when a timestamp cannot be read.
template <typename TimestampAt>
std::optional<size_t> find_faster_blocks_activation_height(
        size_t parent_height, uint64_t activation_time, TimestampAt&& timestamp_at) {
    auto reached = [&](size_t height) -> std::optional<bool> {
        auto const mtp = median_time_past_at(height, timestamp_at);
        if ( ! mtp) {
            return std::nullopt;
        }
        return uint64_t(*mtp) >= activation_time;
    };

    auto const top = reached(parent_height);
    if ( ! top || ! *top) {
        return std::nullopt;
    }

    size_t lo = 0;
    size_t hi = parent_height;
    while (lo < hi) {
        size_t const mid = lo + (hi - lo) / 2;
        auto const r = reached(mid);
        if ( ! r) {
            return std::nullopt;
        }
        if (*r) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_FASTER_BLOCKS_HPP
