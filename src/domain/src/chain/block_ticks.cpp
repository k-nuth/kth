// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/block_ticks.hpp>

#include <algorithm>
#include <utility>

namespace kth::domain::chain {

block_ticks::block_ticks(std::vector<interval_change> schedule)
    : schedule_(std::move(schedule))
{
    tick_at_max_height_ = height_to_tick(max_tick_height);
}

// static
bool block_ticks::is_conforming(std::vector<interval_change> const& schedule) {
    uint64_t prev_height = 0;
    uint64_t prev_tau = base_block_interval;
    uint64_t prev_tick = 0;
    bool first = true;

    for (auto const& e : schedule) {
        if (e.tau < min_block_interval || e.tau > max_block_interval) {
            return false;
        }
        if (base_block_interval % e.tau != 0) {
            return false;
        }
        if (e.height > max_tick_height) {
            return false;
        }
        if ( ! first && e.height <= prev_height) {
            return false;
        }

        // Blocks prev_height + 1 .. e.height still use prev_tau.
        auto const tick = prev_tick + (e.height - prev_height) * prev_tau;
        if (tick % base_block_interval != 0) {
            return false;
        }

        prev_height = e.height;
        prev_tau = e.tau;
        prev_tick = tick;
        first = false;
    }
    return true;
}

// static
std::optional<block_ticks> block_ticks::make(std::vector<interval_change> schedule) {
    if ( ! is_conforming(schedule)) {
        return std::nullopt;
    }
    return block_ticks(std::move(schedule));
}

// static
block_ticks block_ticks::activated_at(uint64_t activation_height) {
    return block_ticks(std::vector<interval_change>{{std::min(activation_height, max_tick_height), faster_block_interval}});
}

uint64_t block_ticks::block_interval(uint64_t height) const noexcept {
    if (height == 0) {
        return base_block_interval;
    }

    // Entry {n, tau} governs blocks > n: the last entry with n < height.
    auto it = std::upper_bound(schedule_.begin(), schedule_.end(), height - 1,
        [](uint64_t h, interval_change const& e) { return h < e.height; });
    if (it == schedule_.begin()) {
        return base_block_interval;
    }
    return (--it)->tau;
}

uint64_t block_ticks::next_block_interval(uint64_t height) const noexcept {
    if (height == UINT64_MAX) {
        return block_interval(UINT64_MAX);
    }
    return block_interval(height + 1);
}

uint64_t block_ticks::height_to_tick(uint64_t height) const noexcept {
    if (height == 0) {
        return 0;
    }
    if (height > max_tick_height) {
        return max_tick;
    }

    uint64_t total = 0;
    uint64_t cursor = 1;
    uint64_t current_tau = base_block_interval;

    auto it = schedule_.begin();
    while (it != schedule_.end() && it->height < height) {
        if (cursor <= it->height) {
            total += (it->height - cursor + 1) * current_tau;
            cursor = it->height + 1;
        }
        current_tau = it->tau;
        ++it;
    }
    if (cursor <= height) {
        total += (height - cursor + 1) * current_tau;
    }
    return total;
}

uint64_t block_ticks::tick_to_height(uint64_t tick) const noexcept {
    if (tick == 0) {
        return 0;
    }
    if (tick >= tick_at_max_height_) {
        return max_tick_height;
    }

    uint64_t cumulative = 0;
    uint64_t cursor = 1;
    uint64_t current_tau = base_block_interval;

    for (auto const& e : schedule_) {
        if (cursor <= e.height) {
            auto const segment_ticks = (e.height - cursor + 1) * current_tau;
            if (cumulative + segment_ticks >= tick) {
                return cursor - 1 + (tick - cumulative) / current_tau;
            }
            cumulative += segment_ticks;
        }
        cursor = e.height + 1;
        current_tau = e.tau;
    }
    return cursor - 1 + (tick - cumulative) / current_tau;
}

} // namespace kth::domain::chain
