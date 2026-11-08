// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_FEE_FILTER_HPP
#define KTH_NODE_DETAIL_FEE_FILTER_HPP

// Not installed. The fee filter (BIP133) the node announces to each peer, as
// BCHN does (net_processing.cpp, SendMessages; policy/fees.cpp,
// FeeFilterRounder).
//
// What is sent is the mempool's minimum fee, rounded to one of a fixed set of
// buckets so that it reveals little about the pool, and never below the relay
// floor. Each peer's service owns that peer's schedule: one send at once, then
// at Poisson intervals averaging ten minutes, and a send within five minutes
// when the minimum moved by more than a quarter of what was last sent.

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <variant>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/experimental/channel_select.hpp>

#include <kth/network/peer_session.hpp>

namespace kth::node::detail {

/// BCHN FeeFilterRounder: buckets from half the minimum incremental fee up to
/// 10,000,000 satoshis per 1000 bytes, each 1.1 times the last, and zero.
class fee_filter_rounder {
public:
    /// BCHN MAX_FEERATE.
    static constexpr uint64_t max_fee_per_kb = 10'000'000;
    /// BCHN FEE_SPACING.
    static constexpr double spacing = 1.1;

    explicit fee_filter_rounder(uint64_t min_incremental_fee_per_kb);

    /// The bucket at or above `fee_per_kb`, or the one below it when
    /// `step_down` (BCHN steps down two times in three) or when it is above
    /// every bucket.
    [[nodiscard]]
    uint64_t round(uint64_t fee_per_kb, bool step_down) const;

    [[nodiscard]]
    std::vector<uint64_t> const& buckets() const { return buckets_; }

private:
    std::vector<uint64_t> buckets_;
};

/// The filter to send: the minimum rounded, and never below the relay floor.
[[nodiscard]]
uint64_t fee_filter_to_send(fee_filter_rounder const& rounder, uint64_t minimum_fee_per_kb,
                            uint64_t relay_floor_per_kb, bool step_down);

/// One peer's fee filter schedule, owned by that peer's service.
class fee_filter_schedule {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    /// BCHN AVG_FEEFILTER_BROADCAST_INTERVAL.
    static constexpr std::chrono::minutes average_interval{10};
    /// BCHN MAX_FEEFILTER_CHANGE_DELAY.
    static constexpr std::chrono::minutes max_change_delay{5};

    /// When the next send is due. The first is due at once.
    [[nodiscard]]
    time_point due() const { return next_; }

    /// The send is due with `to_send`: what to send, if it differs from what
    /// was sent last. The next is due `interval` after `now`.
    std::optional<uint64_t> on_due(uint64_t to_send, time_point now, clock::duration interval);

    /// The minimum moved to `current`. When it moved by more than a quarter of
    /// what was sent last and the next send is more than max_change_delay
    /// away, it is brought forward to `now + delay` (delay < max_change_delay).
    void on_changed(uint64_t current, time_point now, clock::duration delay);

    /// Nothing is due again: the peer gets no fee filter.
    void never() { next_ = time_point::max(); }

    [[nodiscard]]
    std::optional<uint64_t> last_sent() const { return last_sent_; }

private:
    time_point next_{};
    std::optional<uint64_t> last_sent_;
};

/// A uniform draw in [0, 1), for the schedules' random delays.
[[nodiscard]]
double unit_draw();

/// A Poisson-process delay averaging `average` (BCHN PoissonNextSend), from a
/// uniform draw `unit` in [0, 1).
[[nodiscard]]
fee_filter_schedule::clock::duration poisson_delay(fee_filter_schedule::clock::duration average,
                                                   double unit);

/// What the watch reads: the mempool's minimum fee per 1000 bytes now, and
/// the sessions connected now.
struct fee_filter_sources {
    std::function<::asio::awaitable<uint64_t>()> minimum;
    std::function<::asio::awaitable<std::vector<network::peer_session::ptr>>()> peers;
};

/// Each time `events` rings (the minimum may have moved), reads the minimum
/// and hands it to every connected peer's service, which decides whether its
/// next fee filter comes sooner. Ends when `events` is closed or `stop` is
/// requested, also before it begins.
::asio::awaitable<void> fee_filter_watch(
    ::asio::experimental::select_channel<std::monostate>& events,
    fee_filter_sources sources,
    std::stop_token stop);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_FEE_FILTER_HPP
