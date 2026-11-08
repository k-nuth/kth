// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/tx_announcements.hpp>

#include <algorithm>
#include <cmath>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>

#include <kth/node/detail/fee_filter.hpp>

namespace kth::node::detail {

size_t max_announcements(uint64_t block_size_limit) {
    auto const seconds = std::chrono::duration<double>(inventory_broadcast_interval).count();
    return size_t(std::ceil(double(inventory_broadcast_rate)
        * (double(block_size_limit) / 1'000'000.0) * seconds));
}

uint64_t fee_per_kb(blockchain::mempool_entry const& entry) {
    return entry.size == 0 ? 0 : entry.fee * 1000 / entry.size;
}

// tx_announcement_schedule
// -----------------------------------------------------------------------------

tx_announcement_schedule::tx_announcement_schedule(bool inbound, uint64_t cursor)
    : inbound_(inbound)
    , cursor_(cursor)
    , last_(cursor)
{}

void tx_announcement_schedule::on_admitted(network::tx_admissions const& admissions,
                                           time_point now, clock::duration delay) {
    last_ = std::max(last_, admissions.last);
    if (last_ <= cursor_ || due_ != time_point::max()) {
        return;
    }
    due_ = inbound_ ? admissions.inbound_due : now + delay;
}

void tx_announcement_schedule::take(admitted_batch const& batch, bool complete,
                                    network::tx_inventory& inventory, size_t limit,
                                    std::vector<hash_digest>& out) {
    last_ = std::max(last_, batch.last);
    for (auto const& entry : batch.entries) {
        if (out.size() >= limit) {
            // Left for the next announcement, unwalked.
            return;
        }
        cursor_ = entry.entry_id;
        auto const txid = entry.tx->hash();
        if (inventory.known(txid)) {
            continue;
        }
        auto const filter = inventory.fee_filter();
        if (filter != 0 && fee_per_kb(entry) < filter) {
            continue;
        }
        out.push_back(txid);
        inventory.add_announced(txid);
    }
    if (complete) {
        // Up to the last admission, whatever is no longer pooled included.
        cursor_ = std::max(cursor_, batch.last);
    }
}

void tx_announcement_schedule::on_announced(time_point now, clock::duration delay) {
    due_ = cursor_ < last_ ? now + delay : time_point::max();
}

// The watch
// -----------------------------------------------------------------------------

::asio::awaitable<void> tx_announcement_watch(
    ::asio::experimental::select_channel<std::monostate>& events,
    tx_announcement_watch_sources sources,
    std::stop_token stop)
{
    using clock = tx_announcement_schedule::clock;
    auto const executor = co_await ::asio::this_coro::executor;
    // The inbound peers' one schedule (BCHN PoissonNextSendInbound).
    auto inbound_due = clock::time_point::min();
    while ( ! stop.stop_requested()) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(events)
        ).async_wait(executor, {}, stop, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        (void)selected;
        if (ec) {
            break;
        }
        auto const last = co_await sources.last();
        auto const now = clock::now();
        if (inbound_due <= now) {
            inbound_due = now + std::chrono::duration_cast<clock::duration>(
                poisson_delay(inventory_broadcast_interval, sources.draw ? sources.draw() : unit_draw()));
        }
        for (auto const& peer : co_await sources.peers()) {
            peer->announce_admissions(network::tx_admissions{last, inbound_due});
        }
    }
}

} // namespace kth::node::detail
