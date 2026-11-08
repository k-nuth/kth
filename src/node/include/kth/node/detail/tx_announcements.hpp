// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP
#define KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP

// Announcing admitted transactions to peers, as BCHN does (net_processing.cpp,
// RelayTransaction and SendMessages' inventory trickle):
//
// - Every transaction the mempool admits, from a peer or from
//   `sendrawtransaction`, is owed to every peer whose version message asked
//   for transactions. The mempool keeps them in the order it admitted them;
//   each peer's service walks that order from where it last stopped, so what
//   a peer is owed is a position, not a queue, and nothing piles up per peer.
// - Announcements trickle: a peer is sent an `inv` at Poisson intervals, half
//   a second on average for inbound peers, which all share one schedule, and
//   a quarter of a second for outbound ones. Each carries at most 7 per
//   second per MB of the next block's size limit, in order of admission.
// - A transaction the peer already knows (it announced or sent it, or it was
//   announced to it) is skipped, as is one below its fee filter or no longer
//   pooled.
//
// What a peer knows and filters lives in its session (network::tx_inventory).
// The watch below only tells each peer's service how far the order reaches.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <variant>
#include <vector>

#include <asio/awaitable.hpp>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/tx_inventory.hpp>

namespace kth::node::detail {

/// BCHN DEFAULT_INV_BROADCAST_INTERVAL: the average interval between inbound
/// peers' announcements; outbound peers get half.
inline constexpr std::chrono::milliseconds inventory_broadcast_interval{500};

/// BCHN DEFAULT_INV_BROADCAST_RATE: transactions per second per MB of the
/// next block's size limit.
inline constexpr uint64_t inventory_broadcast_rate = 7;

/// BCHN nMaxBroadcasts: what one announcement carries at most,
/// ceil(rate * size limit in MB * interval in seconds).
[[nodiscard]]
size_t max_announcements(uint64_t block_size_limit);

/// The fee per 1000 bytes an entry pays (BCHN CFeeRate::GetFeePerK).
[[nodiscard]]
uint64_t fee_per_kb(blockchain::mempool_entry const& entry);

/// A slice of the mempool's order of admission (block_chain::mempool_admitted).
struct admitted_batch {
    std::vector<blockchain::mempool_entry> entries;
    uint64_t last{0};
};

/// What a peer's service reads to announce.
struct tx_announcement_sources {
    /// The last admission's id.
    std::function<::asio::awaitable<uint64_t>()> last;
    /// Up to `limit` entries still pooled, admitted after `after`, in order.
    std::function<::asio::awaitable<admitted_batch>(uint64_t after, size_t limit)> admitted_after;
    /// What one announcement carries at most (max_announcements).
    std::function<size_t()> limit;
};

/// One peer's announcements, owned by that peer's service.
class tx_announcement_schedule {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    /// Owed what is admitted after `cursor`, the last admission as the peer's
    /// service starts.
    tx_announcement_schedule(bool inbound, uint64_t cursor);

    /// Where the walk stands: every admission up to it was announced or passed.
    [[nodiscard]]
    uint64_t cursor() const { return cursor_; }

    /// When the next announcement is due; time_point::max() when none is owed.
    [[nodiscard]]
    time_point due() const { return due_; }

    /// Admitted up to `admissions.last`. When that is past the cursor and
    /// nothing is due yet, the next announcement is due when the inbound
    /// schedule says for an inbound peer, at `now + delay` for an outbound one.
    void on_admitted(network::tx_admissions const& admissions, time_point now, clock::duration delay);

    /// Walks `batch`, entries after the cursor in order, appending to `out`
    /// what to announce and recording it as known, until `out` holds `limit`.
    /// What the peer knows or filters is passed. `complete` says the batch
    /// reaches `batch.last`: when it was walked to its end, so is the cursor.
    void take(admitted_batch const& batch, bool complete, network::tx_inventory& inventory,
              size_t limit, std::vector<hash_digest>& out);

    /// An announcement was made. While more is owed (past what one carries),
    /// the next is due at `now + delay`; otherwise none is.
    void on_announced(time_point now, clock::duration delay);

    [[nodiscard]]
    bool inbound() const { return inbound_; }

private:
    bool inbound_;
    uint64_t cursor_;
    uint64_t last_;
    time_point due_{time_point::max()};
};

/// What the watch reads: the last admission, the sessions connected now, and
/// a uniform draw in [0, 1) for the inbound schedule.
struct tx_announcement_watch_sources {
    std::function<::asio::awaitable<uint64_t>()> last;
    std::function<::asio::awaitable<std::vector<network::peer_session::ptr>>()> peers;
    std::function<double()> draw;
};

/// Each time `events` rings (transactions were admitted), reads the last
/// admission and hands it to every connected peer's service, with when the
/// inbound peers are next due (one Poisson schedule for all of them, drawn
/// again once it passed). Ends when `events` is closed or `stop` is
/// requested, also before it begins.
::asio::awaitable<void> tx_announcement_watch(
    ::asio::experimental::select_channel<std::monostate>& events,
    tx_announcement_watch_sources sources,
    std::stop_token stop);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_TX_ANNOUNCEMENTS_HPP
