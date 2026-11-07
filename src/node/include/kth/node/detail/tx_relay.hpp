// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_TX_RELAY_HPP
#define KTH_NODE_DETAIL_TX_RELAY_HPP

// Not installed. Transactions peers announce, asked for and offered to the
// mempool.
//
// Who owns what:
//
//   - Everything the relay knows — which peer announced which transaction,
//     which request is out and until when, what was rejected — belongs to one
//     coroutine, tx_relay_task. Nothing else touches it, so it needs no lock:
//     `tx_request_tracker` and `recent_rejects` below are plain state.
//   - Read loops reach it through a bounded command channel, and only with
//     try_send. A read loop never waits on the relay: what it cannot hand over
//     is dropped there, and the tracker's deadlines recover what that loses.
//   - The owner never waits on a peer or on validation. A `getdata` goes out
//     from a task of its own, one at a time per peer; a transaction goes to
//     the admission task through a bounded channel. Both report back through
//     the command channel, waiting for room: an answer they dropped would
//     leave a peer marked busy, or a transaction marked held, forever.
//
// The request rules are BCHN's TxRequestTracker (net_processing.cpp): every
// announcement is remembered with the peer that made it; one peer at a time is
// asked; a request that is not answered within GETDATA_TX_INTERVAL, or that
// the peer answers with `notfound`, or whose peer disconnects, passes the
// transaction to the next peer that announced it. Outbound peers are asked
// first; the others, and peers with MAX_PEER_TX_REQUEST_IN_FLIGHT requests
// out, only after a delay. A peer may have MAX_PEER_TX_ANNOUNCEMENTS
// announcements tracked at once.

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <asio/awaitable.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node::detail {

// -----------------------------------------------------------------------------
// The requests, as state
// -----------------------------------------------------------------------------

/// Which peer to ask for which announced transaction, and when.
class tx_request_tracker {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    /// BCHN MAX_PEER_TX_ANNOUNCEMENTS.
    static constexpr size_t max_peer_announcements = 2 * max_inventory;
    /// BCHN MAX_PEER_TX_REQUEST_IN_FLIGHT: past it, the peer's new
    /// announcements wait `overloaded_delay` more.
    static constexpr size_t max_peer_in_flight = 100;
    /// BCHN NONPREF_PEER_TX_DELAY.
    static constexpr std::chrono::seconds nonpreferred_delay{2};
    /// BCHN OVERLOADED_PEER_TX_DELAY.
    static constexpr std::chrono::seconds overloaded_delay{2};
    /// BCHN GETDATA_TX_INTERVAL: how long a request may stay unanswered.
    static constexpr std::chrono::seconds request_interval{60};

    /// What to ask one peer for.
    struct request {
        uint64_t peer;
        hash_list txids;
    };

    /// What take_requests() decided: what to ask now, and what was asked
    /// before and is overdue, no longer to be sent if it has not left yet.
    struct due {
        std::vector<request> asked;
        std::vector<request> expired;
    };

    /// `peer` announced `txid`. False when it is not tracked: the peer
    /// announced it already, or has max_peer_announcements tracked.
    bool announced(uint64_t peer, bool preferred, hash_digest const& txid, time_point now);

    /// `peer` said it does not have `txid`.
    void not_found(uint64_t peer, hash_digest const& txid);

    /// `peer` sent `txid`, asked for or not.
    void received(uint64_t peer, hash_digest const& txid);

    /// `txid` is being validated: nobody is asked for it until forget().
    void hold(hash_digest const& txid);

    /// `peer` delivered `txid` and nothing could take it: validation was
    /// full. Its announcement is kept, asked of nobody, until unpark(); a
    /// delivery nobody announced becomes the peer's announcement, within its
    /// max_peer_announcements. False when that bound refused it.
    bool park(uint64_t peer, hash_digest const& txid);

    /// Validation has room again: what was parked is due now, from the peer
    /// that delivered it.
    void unpark(time_point now);

    /// Whether `peer` is asked for `txid` now.
    [[nodiscard]]
    bool requested_from(uint64_t peer, hash_digest const& txid) const;

    /// Transactions parked.
    [[nodiscard]]
    size_t parked() const { return parked_.size(); }

    /// Whether `txid` is being validated.
    [[nodiscard]]
    bool held(hash_digest const& txid) const;

    /// Done with `txid`: admitted, rejected or already known. Every
    /// announcement of it goes.
    void forget(hash_digest const& txid);

    /// `peer` is gone. What it was asked for passes to the next announcer.
    void disconnected(uint64_t peer);

    /// Expire what is overdue and choose whom to ask for what is due, at
    /// most one peer per transaction. What is returned counts as asked.
    [[nodiscard]]
    due take_requests(time_point now);

    /// When take_requests() next has something to do. Nothing when idle.
    [[nodiscard]]
    std::optional<time_point> next_wakeup() const;

    [[nodiscard]]
    size_t in_flight(uint64_t peer) const;

    [[nodiscard]]
    size_t announcements(uint64_t peer) const;

    [[nodiscard]]
    bool tracking(hash_digest const& txid) const;

    /// Transactions tracked.
    [[nodiscard]]
    size_t size() const { return entries_.size(); }

private:
    enum class status { candidate, requested, completed, parked };

    struct announcement {
        uint64_t peer;
        bool preferred;
        // A candidate's earliest request time; a request's deadline.
        time_point at;
        uint64_t sequence;
        status state;
    };

    struct entry {
        std::vector<announcement> announcements;
        bool held{false};
        std::optional<time_point> scheduled;
    };

    struct peer_state {
        size_t in_flight{0};
        boost::unordered_flat_set<hash_digest> txids;
    };

    void complete(announcement& item);
    void complete(uint64_t peer, hash_digest const& txid);
    // Reschedule `txid`, or drop it when nothing is left to do for it.
    void settle(hash_digest const& txid);
    void unschedule(entry& item, hash_digest const& txid);
    void drop(hash_digest const& txid, entry& item);

    boost::unordered_flat_map<hash_digest, entry> entries_;
    boost::unordered_flat_map<uint64_t, peer_state> peers_;
    std::set<std::pair<time_point, hash_digest>> schedule_;
    boost::unordered_flat_set<hash_digest> parked_;
    uint64_t sequence_{0};
};

/// Transactions validation rejected, not asked for again until the tip moves
/// (BCHN recentRejects). Bounded: the oldest are forgotten first.
class recent_rejects {
public:
    /// BCHN sizes its filter for 120,000 entries.
    static constexpr size_t capacity = 120'000;

    void insert(hash_digest const& txid);

    [[nodiscard]]
    bool contains(hash_digest const& txid) const;

    /// A new tip may make any of them valid.
    void reset_for(hash_digest const& tip);

    [[nodiscard]]
    size_t size() const { return set_.size(); }

private:
    hash_digest tip_{null_hash};
    boost::unordered_flat_set<hash_digest> set_;
    std::deque<hash_digest> order_;
};

// -----------------------------------------------------------------------------
// The channels around it
// -----------------------------------------------------------------------------

/// A peer's `inv`, its transaction entries.
struct tx_announced {
    network::peer_session::ptr peer;
    hash_list txids;
};

/// A peer's `notfound`, its transaction entries.
struct tx_not_found {
    uint64_t peer;
    hash_list txids;
};

/// A peer's `tx`.
struct tx_received {
    uint64_t peer;
    domain::message::transaction::const_ptr tx;
};

/// A session ended.
struct tx_peer_gone {
    uint64_t peer;
};

/// A `getdata` left, or could not: the peer may be sent the next.
struct tx_request_sent {
    uint64_t peer;
    bool sent;
};

/// Validation's answer for a transaction the relay handed over.
struct tx_admitted {
    hash_digest txid;
    code result;
};

using tx_relay_command = std::variant<tx_announced, tx_not_found, tx_received,
    tx_peer_gone, tx_request_sent, tx_admitted>;

/// The transactions handed to validation, one at a time.
struct tx_admission {
    domain::message::transaction::const_ptr tx;
};

/// Transactions handed to validation and not yet taken by it.
inline constexpr size_t tx_admission_capacity = 64;

/// Transactions the owner keeps, in order, while validation is full. Past
/// it a transaction is not kept, but its announcement is parked and asked
/// again when validation has room (tx_request_tracker::park).
inline constexpr size_t tx_waiting_capacity = 256;

using tx_admission_channel = select_channel<tx_admission>;

/// What the read loops and the relay task share. Created and closed by the
/// node.
struct tx_relay_channels {
    /// Submissions between two turns of the owner, which takes each at once.
    static constexpr size_t command_capacity = 1024;

    tx_relay_channels();

    tx_relay_channels(tx_relay_channels const&) = delete;
    tx_relay_channels& operator=(tx_relay_channels const&) = delete;

    /// From a read loop, never waiting. False when refused.
    bool submit(tx_relay_command command);

    /// End it: the owner sees the channel closed.
    void close();

    select_channel<tx_relay_command> commands;
};

/// What one read loop handed the relay and the relay could not take yet,
/// owned by that read loop. Kept in order and handed over again at the read
/// loop's next turn: the next message from its peer, or the short retry
/// deadline it waits with while this holds something. Bounded per peer as
/// BCHN bounds a peer's announcements: past the bound, the peer's excess is
/// what is not kept.
class tx_relay_backlog {
public:
    /// Transaction ids kept, from `inv` and `notfound` together.
    static constexpr size_t max_txids = tx_request_tracker::max_peer_announcements;
    /// Transactions kept.
    static constexpr size_t max_transactions = 64;
    /// How soon the read loop hands over again while this holds something.
    static constexpr std::chrono::milliseconds retry_interval{250};

    /// Hand `command` over now when nothing is kept before it, or keep it.
    /// False when it is neither: past the bound.
    bool submit(tx_relay_channels& channels, tx_relay_command command);

    /// Hand over what is kept, in order, until the relay refuses one.
    void flush(tx_relay_channels& channels);

    [[nodiscard]]
    bool empty() const { return pending_.empty(); }

    [[nodiscard]]
    size_t size() const { return pending_.size(); }

private:
    bool keep(tx_relay_command command);

    std::deque<tx_relay_command> pending_;
    size_t txids_{0};
    size_t transactions_{0};
};

// -----------------------------------------------------------------------------
// The tasks
// -----------------------------------------------------------------------------

/// What the owner reads from the chain. Each must be safe to call from the
/// owner's executor while the chain runs.
struct tx_relay_view {
    /// Announcements and transactions are ignored until this holds: the node
    /// has nothing current to check them against.
    std::function<bool()> synchronized;
    std::function<bool(hash_digest const&)> in_mempool;
    /// The connected tip, which clears the recent rejects when it moves.
    std::function<hash_digest()> tip;
    /// The clock the request deadlines are read on. The steady clock when
    /// empty; controls set their own.
    std::function<tx_request_tracker::time_point()> now = {};
};

/// Hands a task to the caller's task group.
using tx_relay_spawner = std::function<void(::asio::awaitable<void>)>;

/// The owner. Ends when `channels` is closed, and then closes `admission`.
::asio::awaitable<void> tx_relay_task(
    tx_relay_channels& channels,
    tx_relay_view view,
    tx_admission_channel& admission,
    tx_relay_spawner spawn);

/// Offers each transaction to the mempool and reports the result to the
/// owner. Ends when `admission` is closed, after the one in hand.
using tx_submit_fn = std::function<::asio::awaitable<code>(domain::message::transaction::const_ptr)>;

::asio::awaitable<void> tx_admission_task(
    tx_admission_channel& admission,
    tx_submit_fn submit,
    tx_relay_channels& channels);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_TX_RELAY_HPP
