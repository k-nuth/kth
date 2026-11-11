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
//     which request is out and until when, what was rejected, which
//     transactions wait for their parents — belongs to one coroutine,
//     tx_relay_task. Nothing else touches it, so it needs no lock:
//     `tx_request_tracker`, `recent_rejects` and `tx_orphanage` below are
//     plain state.
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
//
// A transaction whose inputs validation did not find is an orphan, handled as
// BCHN does (net_processing.cpp, TX_MISSING_INPUTS; txorphanage.cpp): when
// one of the transactions it spends was rejected, so is it; otherwise those
// not known are asked of the peer that sent it, as if that peer had
// announced them, and it is kept until one of them reaches the mempool, then
// validated again. A peer's orphans go when it does, and so do those a
// connected block resolves: what spends an output the block spends, the block's
// own transactions among them (BCHN EraseForBlock).
//
// The blocks reach the owner without it waiting on storage: when the connected
// tip moves, a one-slot doorbell wakes it, and a task of its own reads the
// blocks from the last one the owner saw up to the tip and hands them over
// through the command channel, waiting for room. The owner keeps where it got
// to, so rings that coalesce lose nothing; with no orphans it only moves that
// mark, and reads nothing.

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

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/message/block.hpp>
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

    /// Up to `count` of what `peer` is asked for and has not been sent yet,
    /// now counted as sent. What leaves the asked state before it is taken
    /// here (expired, not found, delivered, forgotten, its peer gone) is
    /// never taken: what waits to be sent is the tracker's own, within its
    /// bounds.
    [[nodiscard]]
    hash_list take_unsent(uint64_t peer, size_t count);

    [[nodiscard]]
    size_t unsent(uint64_t peer) const;

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
        // Asked of this peer, not yet in a getdata.
        boost::unordered_flat_set<hash_digest> unsent;
    };

    void complete(announcement& item, hash_digest const& txid);
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

/// The transactions handed to validation, one at a time, with the peer each
/// came from.
struct tx_admission {
    domain::message::transaction::const_ptr tx;
    uint64_t peer;
};

/// Transactions validation could not judge because inputs were missing,
/// kept until a parent reaches the mempool (BCHN TxOrphanage). Bounded:
/// what expires is swept, and past the capacity one is dropped at random.
class tx_orphanage {
public:
    using clock = tx_request_tracker::clock;
    using time_point = tx_request_tracker::time_point;

    /// BCHN DEFAULT_MAX_ORPHAN_TRANSACTIONS.
    static constexpr size_t capacity = 100;
    /// BCHN MAX_STANDARD_TX_SIZE: a larger transaction is not kept.
    static constexpr size_t max_transaction_size = 100'000;
    /// BCHN ORPHAN_TX_EXPIRE_TIME.
    static constexpr std::chrono::minutes expire_time{20};
    /// BCHN ORPHAN_TX_EXPIRE_INTERVAL: how often what expired is swept.
    static constexpr std::chrono::minutes sweep_interval{5};

    /// Picks an index below its argument: which one goes past the capacity.
    /// Uniformly random when empty; controls set their own.
    using pick_fn = std::function<size_t(size_t)>;

    explicit tx_orphanage(pick_fn pick = {});

    /// Keep `tx`, from `peer`, until a parent of it reaches the mempool. False
    /// when it is kept already or too large. Then, as BCHN does on each
    /// addition, what expired goes (at most once per sweep_interval) and,
    /// past the capacity, one at random until it is within it.
    bool add(domain::message::transaction::const_ptr tx, uint64_t peer, time_point now);

    [[nodiscard]]
    bool contains(hash_digest const& txid) const;

    /// `parent` reached the mempool: what spends it is ready to be validated
    /// again, in the order it becomes ready.
    void parent_admitted(hash_digest const& parent);

    /// The first one ready, still kept: erase() it once validation takes it.
    [[nodiscard]]
    std::optional<tx_admission> next_ready() const;

    void erase(hash_digest const& txid);

    /// `peer` is gone: what it sent goes (BCHN EraseForPeer).
    void erase_for_peer(uint64_t peer);

    /// `block` was connected: what spends an output it spends goes, its own
    /// transactions among them (BCHN EraseForBlock). Returns how many went.
    size_t erase_for_block(domain::chain::block const& block);

    [[nodiscard]]
    size_t size() const { return orphans_.size(); }

    [[nodiscard]]
    size_t ready() const { return ready_.size(); }

private:
    struct orphan {
        domain::message::transaction::const_ptr tx;
        uint64_t peer;
        time_point expires;
        bool ready{false};
    };

    void sweep(time_point now);

    boost::unordered_flat_map<hash_digest, orphan> orphans_;
    // Each kept transaction under every transaction its inputs spend.
    boost::unordered_flat_map<hash_digest, boost::unordered_flat_set<hash_digest>> by_parent_;
    // What is kept, for a uniform pick.
    std::vector<hash_digest> order_;
    std::deque<hash_digest> ready_;
    time_point next_sweep_{};
    pick_fn pick_;
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

/// A peer's `tx`, with its session: a peer that sends without announcing is
/// asked for the parents of what it sends all the same.
struct tx_received {
    uint64_t peer;
    domain::message::transaction::const_ptr tx;
    network::peer_session::ptr session = {};
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

/// A block of the connected chain, by hash and height.
struct chain_point {
    hash_digest hash;
    size_t height;

    friend bool operator==(chain_point const&, chain_point const&) = default;
};

/// A connected block the owner had not seen, read by the scan, in order.
struct tx_block_connected {
    domain::message::block::const_ptr block;
    chain_point point;
};

/// The scan ended: the owner has seen the connected chain up to `reached`.
struct tx_scan_ended {
    chain_point reached;
};

/// Validation's answer for a transaction the relay handed over: the
/// transaction, the peer it came from, and, when inputs were missing, the
/// transactions they spend that validation did not find.
struct tx_admitted {
    domain::message::transaction::const_ptr tx;
    uint64_t peer;
    code result;
    hash_list missing_parents = {};
};

using tx_relay_command = std::variant<tx_announced, tx_not_found, tx_received,
    tx_peer_gone, tx_request_sent, tx_admitted, tx_block_connected, tx_scan_ended>;

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

    /// The connected tip moved. Never waits: rings coalesce, and the owner
    /// reads from where it got to.
    void tip_moved();

    /// End it: the owner sees the channel closed.
    void close();

    select_channel<tx_relay_command> commands;
    select_channel<std::monostate> tip_events;
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
    /// The connected tip with its height, where reading connected blocks for
    /// the orphans ends. nullopt before the chain has one. When empty, no
    /// block is read and orphans go only by expiry, size or peer.
    std::function<std::optional<chain_point>()> connected_tip = {};
    /// The block at `height` on the active chain; null when it cannot be read.
    std::function<::asio::awaitable<domain::message::block::const_ptr>(size_t)> block_at = {};
    /// The parent of the block `hash`; nullopt when it is not known.
    std::function<::asio::awaitable<std::optional<chain_point>>(hash_digest)> parent_of = {};
    /// The clock the request deadlines are read on. The steady clock when
    /// empty; controls set their own.
    std::function<tx_request_tracker::time_point()> now = {};
};

/// What the owner reads from `chain`: the node's view.
tx_relay_view chain_relay_view(blockchain::block_chain& chain);

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
