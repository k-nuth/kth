// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BLOCK_TICKS_HPP
#define KTH_DOMAIN_CHAIN_BLOCK_TICKS_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include <kth/domain/define.hpp>

namespace kth::domain::chain {

// CHIP-2025-03 height <-> tick mapping.
//
// The genesis block ends at tick 0. Block h > 0 adds tau(h) ticks, so
// T(h) = T(h - 1) + tau(h). The default tau is `base_block_interval` (600).
//
// A schedule entry {height, tau} (the CHIP's {H_a, next_tau}) means: block
// `height` keeps the previous interval; block height + 1 and every later one
// uses `tau`, until the next entry. Entries are strictly ascending, tau lies in
// [1, 600], divides 600, and tick(height) is a multiple of 600.
//
// The schedule is immutable once built and owned by value, so it is a
// branch-local context: nothing here is shared between branches or threads.
inline constexpr uint64_t base_block_interval = 600;
inline constexpr uint64_t max_block_interval = 600;
inline constexpr uint64_t min_block_interval = 1;

// One below the largest height whose cumulative tick still fits in uint64_t.
inline constexpr uint64_t max_tick_height = UINT64_MAX / max_block_interval - 1;
inline constexpr uint64_t max_tick = max_tick_height * max_block_interval;

// Interval after the faster-blocks activation (CHIP-2025-03): 1 minute.
inline constexpr uint64_t faster_block_interval = 60;

struct interval_change {
    uint64_t height;    // H_a: last block with the previous tau
    uint64_t tau;       // ticks added by each block after `height`

    friend bool operator==(interval_change const&, interval_change const&) = default;
};

class KD_API block_ticks {
public:
    // Uniform 600-tick schedule (every pre-activation branch).
    block_ticks() = default;

    // Validates the CHIP schedule constraints; nullopt when violated.
    static
    std::optional<block_ticks> make(std::vector<interval_change> schedule);

    // The activation schedule {{activation_height, faster_block_interval}}.
    // Every height is conforming: all earlier blocks add 600 ticks.
    static
    block_ticks activated_at(uint64_t activation_height);

    [[nodiscard]]
    std::vector<interval_change> const& schedule() const noexcept {
        return schedule_;
    }

    // Cumulative tick of block `height`, inclusive of its own interval.
    // Heights above max_tick_height saturate to max_tick.
    [[nodiscard]]
    uint64_t height_to_tick(uint64_t height) const noexcept;

    // Greatest height whose cumulative tick is <= `tick` (saturating).
    [[nodiscard]]
    uint64_t tick_to_height(uint64_t tick) const noexcept;

    // Target interval of block `height` itself (genesis: 600 by convention).
    [[nodiscard]]
    uint64_t block_interval(uint64_t height) const noexcept;

    // Target interval of block `height + 1`.
    [[nodiscard]]
    uint64_t next_block_interval(uint64_t height) const noexcept;

    // Tick of the not-yet-mined block `tip + 1`. Taken from the schedule,
    // never extrapolated as tick(tip) + interval(tip).
    [[nodiscard]]
    uint64_t next_block_tick(uint64_t tip) const noexcept {
        return height_to_tick(tip) + next_block_interval(tip);
    }

    friend
    bool operator==(block_ticks const&, block_ticks const&) = default;

private:
    explicit
    block_ticks(std::vector<interval_change> schedule);

    static
    bool is_conforming(std::vector<interval_change> const& schedule);

    std::vector<interval_change> schedule_;
    uint64_t tick_at_max_height_ = max_tick;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_BLOCK_TICKS_HPP
