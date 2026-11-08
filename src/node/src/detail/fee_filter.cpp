// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/fee_filter.hpp>

#include <kth/infrastructure/utility/pseudo_random.hpp>

#include <algorithm>
#include <cmath>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>

namespace kth::node::detail {

fee_filter_rounder::fee_filter_rounder(uint64_t min_incremental_fee_per_kb) {
    buckets_.push_back(0);
    auto const lowest = std::max<uint64_t>(1, min_incremental_fee_per_kb / 2);
    for (double boundary = double(lowest); boundary <= double(max_fee_per_kb); boundary *= spacing) {
        buckets_.push_back(uint64_t(boundary));
    }
    // Truncation can repeat a value at the low end; a set has none.
    buckets_.erase(std::unique(buckets_.begin(), buckets_.end()), buckets_.end());
}

uint64_t fee_filter_rounder::round(uint64_t fee_per_kb, bool step_down) const {
    auto it = std::lower_bound(buckets_.begin(), buckets_.end(), fee_per_kb);
    if ((it != buckets_.begin() && step_down) || it == buckets_.end()) {
        --it;
    }
    return *it;
}

uint64_t fee_filter_to_send(fee_filter_rounder const& rounder, uint64_t minimum_fee_per_kb,
                            uint64_t relay_floor_per_kb, bool step_down) {
    return std::max(rounder.round(minimum_fee_per_kb, step_down), relay_floor_per_kb);
}

std::optional<uint64_t> fee_filter_schedule::on_due(uint64_t to_send, time_point now,
                                                    clock::duration interval) {
    next_ = now + interval;
    if (last_sent_ == to_send) {
        return std::nullopt;
    }
    last_sent_ = to_send;
    return to_send;
}

void fee_filter_schedule::on_changed(uint64_t current, time_point now, clock::duration delay) {
    if (next_ == time_point::max()) {
        return;                                     // this peer gets none
    }
    auto const last = last_sent_.value_or(0);
    auto const moved = current < 3 * last / 4 || current > 4 * last / 3;
    if (moved && now + max_change_delay < next_) {
        next_ = now + delay;
    }
}

fee_filter_schedule::clock::duration poisson_delay(fee_filter_schedule::clock::duration average,
                                                   double unit) {
    auto const scale = -std::log1p(-std::clamp(unit, 0.0, 0.999999999));
    return std::chrono::duration_cast<fee_filter_schedule::clock::duration>(average * scale);
}

double unit_draw() {
    return double(pseudo_random::generate<uint64_t>(0, (uint64_t(1) << 53) - 1)) / double(uint64_t(1) << 53);
}

::asio::awaitable<void> fee_filter_watch(
    ::asio::experimental::select_channel<std::monostate>& events,
    fee_filter_sources sources,
    std::stop_token stop)
{
    auto const executor = co_await ::asio::this_coro::executor;
    while ( ! stop.stop_requested()) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(events)
        ).async_wait(executor, {}, stop, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        (void)selected;
        if (ec) {
            break;
        }
        auto const minimum = co_await sources.minimum();
        for (auto const& peer : co_await sources.peers()) {
            peer->announce_fee_filter(minimum);
        }
    }
}

} // namespace kth::node::detail
