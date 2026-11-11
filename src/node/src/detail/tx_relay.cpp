// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/tx_relay.hpp>

#include <algorithm>
#include <tuple>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/error.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/this_coro.hpp>
#include <spdlog/spdlog.h>

#include <kth/domain/message/get_data.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::node::detail {

// =============================================================================
// tx_request_tracker
// =============================================================================

bool tx_request_tracker::announced(uint64_t peer, bool preferred, hash_digest const& txid,
                                   time_point now) {
    auto& counts = peers_[peer];
    if (counts.txids.size() >= max_peer_announcements || counts.txids.contains(txid)) {
        return false;
    }

    auto delay = time_point::duration::zero();
    if ( ! preferred) {
        delay += nonpreferred_delay;
    }
    if (counts.in_flight >= max_peer_in_flight) {
        delay += overloaded_delay;
    }

    counts.txids.insert(txid);
    entries_[txid].announcements.push_back(
        {peer, preferred, now + delay, sequence_++, status::candidate});
    settle(txid);
    return true;
}

void tx_request_tracker::not_found(uint64_t peer, hash_digest const& txid) {
    complete(peer, txid);
}

void tx_request_tracker::received(uint64_t peer, hash_digest const& txid) {
    complete(peer, txid);
}

void tx_request_tracker::hold(hash_digest const& txid) {
    auto& item = entries_[txid];
    item.held = true;
    unschedule(item, txid);
}

bool tx_request_tracker::held(hash_digest const& txid) const {
    auto const it = entries_.find(txid);
    return it != entries_.end() && it->second.held;
}

void tx_request_tracker::forget(hash_digest const& txid) {
    auto const it = entries_.find(txid);
    if (it == entries_.end()) {
        return;
    }
    drop(txid, it->second);
}

void tx_request_tracker::disconnected(uint64_t peer) {
    auto const it = peers_.find(peer);
    if (it == peers_.end()) {
        return;
    }
    auto const txids = std::move(it->second.txids);
    peers_.erase(it);

    for (auto const& txid : txids) {
        auto const found = entries_.find(txid);
        if (found == entries_.end()) {
            continue;
        }
        std::erase_if(found->second.announcements, [peer](announcement const& item) {
            return item.peer == peer;
        });
        settle(txid);
    }
}

tx_request_tracker::due tx_request_tracker::take_requests(time_point now) {
    boost::unordered_flat_map<uint64_t, hash_list> chosen;
    boost::unordered_flat_map<uint64_t, hash_list> overdue;

    while ( ! schedule_.empty() && schedule_.begin()->first <= now) {
        auto const txid = schedule_.begin()->second;
        schedule_.erase(schedule_.begin());
        auto& item = entries_.at(txid);
        item.scheduled.reset();

        // What is overdue failed: it frees its peer and the transaction.
        for (auto& announced : item.announcements) {
            if (announced.state == status::requested && announced.at <= now) {
                complete(announced, txid);
                overdue[announced.peer].push_back(txid);
            }
        }

        auto const asked = std::ranges::any_of(item.announcements, [](announcement const& a) {
            return a.state == status::requested;
        });
        if ( ! item.held && ! asked) {
            // The best due candidate: outbound first, then the earliest, then
            // the first to announce.
            announcement* best = nullptr;
            for (auto& candidate : item.announcements) {
                if (candidate.state != status::candidate || candidate.at > now) {
                    continue;
                }
                if (best == nullptr
                    || std::tuple( ! candidate.preferred, candidate.at, candidate.sequence)
                     < std::tuple( ! best->preferred, best->at, best->sequence)) {
                    best = &candidate;
                }
            }
            if (best != nullptr) {
                best->state = status::requested;
                best->at = now + request_interval;
                auto& asked_peer = peers_[best->peer];
                ++asked_peer.in_flight;
                asked_peer.unsent.insert(txid);
                chosen[best->peer].push_back(txid);
            }
        }
        settle(txid);
    }

    due out;
    out.asked.reserve(chosen.size());
    for (auto& [peer, txids] : chosen) {
        out.asked.push_back({peer, std::move(txids)});
    }
    for (auto& [peer, txids] : overdue) {
        out.expired.push_back({peer, std::move(txids)});
    }
    return out;
}

bool tx_request_tracker::park(uint64_t peer, hash_digest const& txid) {
    auto& counts = peers_[peer];
    auto& item = entries_[txid];
    auto it = std::ranges::find_if(item.announcements, [peer](announcement const& a) {
        return a.peer == peer;
    });
    if (it == item.announcements.end()) {
        if (counts.txids.size() >= max_peer_announcements) {
            settle(txid);
            return false;
        }
        counts.txids.insert(txid);
        item.announcements.push_back({peer, false, time_point::max(), sequence_++, status::parked});
    } else {
        complete(*it, txid);
        it->state = status::parked;
        it->at = time_point::max();
    }
    parked_.insert(txid);
    settle(txid);
    return true;
}

void tx_request_tracker::unpark(time_point now) {
    auto const parked = std::move(parked_);
    parked_.clear();
    for (auto const& txid : parked) {
        auto const found = entries_.find(txid);
        if (found == entries_.end()) {
            continue;
        }
        for (auto& announced : found->second.announcements) {
            if (announced.state == status::parked) {
                announced.state = status::candidate;
                announced.at = now;
            }
        }
        settle(txid);
    }
}

hash_list tx_request_tracker::take_unsent(uint64_t peer, size_t count) {
    hash_list out;
    auto const it = peers_.find(peer);
    if (it == peers_.end()) {
        return out;
    }
    auto& unsent = it->second.unsent;
    for (auto txid = unsent.begin(); txid != unsent.end() && out.size() < count; ) {
        out.push_back(*txid);
        txid = unsent.erase(txid);
    }
    return out;
}

size_t tx_request_tracker::unsent(uint64_t peer) const {
    auto const it = peers_.find(peer);
    return it == peers_.end() ? 0 : it->second.unsent.size();
}

bool tx_request_tracker::requested_from(uint64_t peer, hash_digest const& txid) const {
    auto const it = entries_.find(txid);
    if (it == entries_.end()) {
        return false;
    }
    return std::ranges::any_of(it->second.announcements, [peer](announcement const& a) {
        return a.peer == peer && a.state == status::requested;
    });
}

std::optional<tx_request_tracker::time_point> tx_request_tracker::next_wakeup() const {
    if (schedule_.empty()) {
        return std::nullopt;
    }
    return schedule_.begin()->first;
}

size_t tx_request_tracker::in_flight(uint64_t peer) const {
    auto const it = peers_.find(peer);
    return it == peers_.end() ? 0 : it->second.in_flight;
}

size_t tx_request_tracker::announcements(uint64_t peer) const {
    auto const it = peers_.find(peer);
    return it == peers_.end() ? 0 : it->second.txids.size();
}

bool tx_request_tracker::tracking(hash_digest const& txid) const {
    return entries_.contains(txid);
}

void tx_request_tracker::complete(announcement& item, hash_digest const& txid) {
    if (item.state == status::requested) {
        auto const it = peers_.find(item.peer);
        if (it != peers_.end()) {
            if (it->second.in_flight > 0) {
                --it->second.in_flight;
            }
            it->second.unsent.erase(txid);
        }
    }
    item.state = status::completed;
}

void tx_request_tracker::complete(uint64_t peer, hash_digest const& txid) {
    auto const it = entries_.find(txid);
    if (it == entries_.end()) {
        return;
    }
    for (auto& item : it->second.announcements) {
        if (item.peer == peer) {
            complete(item, txid);
        }
    }
    settle(txid);
}

void tx_request_tracker::settle(hash_digest const& txid) {
    auto const it = entries_.find(txid);
    if (it == entries_.end()) {
        return;
    }
    auto& item = it->second;
    unschedule(item, txid);
    if (item.held) {
        return;
    }

    // A request out: its deadline. Otherwise the earliest candidate. A parked
    // announcement is kept but schedules nothing: unpark() does.
    std::optional<time_point> when;
    bool parked = false;
    for (auto const& announced : item.announcements) {
        if (announced.state == status::requested) {
            when = announced.at;
            break;
        }
        if (announced.state == status::candidate && ( ! when || announced.at < *when)) {
            when = announced.at;
        }
        parked = parked || announced.state == status::parked;
    }
    if ( ! when) {
        if ( ! parked) {
            // Nobody left to ask: every announcer answered, failed or left.
            drop(txid, item);
        }
        return;
    }
    item.scheduled = *when;
    schedule_.emplace(*when, txid);
}

void tx_request_tracker::unschedule(entry& item, hash_digest const& txid) {
    if (item.scheduled) {
        schedule_.erase({*item.scheduled, txid});
        item.scheduled.reset();
    }
}

void tx_request_tracker::drop(hash_digest const& txid, entry& item) {
    unschedule(item, txid);
    parked_.erase(txid);
    for (auto& announced : item.announcements) {
        complete(announced, txid);
        auto const it = peers_.find(announced.peer);
        if (it != peers_.end()) {
            it->second.txids.erase(txid);
        }
    }
    entries_.erase(txid);
}

// =============================================================================
// recent_rejects
// =============================================================================

void recent_rejects::insert(hash_digest const& txid) {
    if ( ! set_.insert(txid).second) {
        return;
    }
    order_.push_back(txid);
    if (order_.size() > capacity) {
        set_.erase(order_.front());
        order_.pop_front();
    }
}

bool recent_rejects::contains(hash_digest const& txid) const {
    return set_.contains(txid);
}

void recent_rejects::reset_for(hash_digest const& tip) {
    if (tip == tip_) {
        return;
    }
    tip_ = tip;
    set_.clear();
    order_.clear();
}

// =============================================================================
// tx_orphanage
// =============================================================================

tx_orphanage::tx_orphanage(pick_fn pick)
    : pick_(std::move(pick))
{
    if ( ! pick_) {
        pick_ = [](size_t size) { return pseudo_random::generate<size_t>(0, size - 1); };
    }
}

bool tx_orphanage::add(domain::message::transaction::const_ptr tx, uint64_t peer, time_point now) {
    auto const txid = tx->hash();
    if (orphans_.contains(txid) || tx->serialized_size(true) > max_transaction_size) {
        return false;
    }
    for (auto const& input : tx->inputs()) {
        by_parent_[input.previous_output().hash()].insert(txid);
    }
    orphans_.emplace(txid, orphan{std::move(tx), peer, now + expire_time});
    order_.push_back(txid);

    sweep(now);
    while (orphans_.size() > capacity) {
        auto const victim = order_[pick_(order_.size())];
        erase(victim);
    }
    return true;
}

bool tx_orphanage::contains(hash_digest const& txid) const {
    return orphans_.contains(txid);
}

void tx_orphanage::parent_admitted(hash_digest const& parent) {
    auto const it = by_parent_.find(parent);
    if (it == by_parent_.end()) {
        return;
    }
    for (auto const& child : it->second) {
        auto& item = orphans_.at(child);
        if ( ! item.ready) {
            item.ready = true;
            ready_.push_back(child);
        }
    }
}

std::optional<tx_admission> tx_orphanage::next_ready() const {
    if (ready_.empty()) {
        return std::nullopt;
    }
    auto const& item = orphans_.at(ready_.front());
    return tx_admission{item.tx, item.peer};
}

void tx_orphanage::erase(hash_digest const& txid) {
    auto const it = orphans_.find(txid);
    if (it == orphans_.end()) {
        return;
    }
    for (auto const& input : it->second.tx->inputs()) {
        auto const parent = by_parent_.find(input.previous_output().hash());
        if (parent == by_parent_.end()) {
            continue;
        }
        parent->second.erase(txid);
        if (parent->second.empty()) {
            by_parent_.erase(parent);
        }
    }
    if (it->second.ready) {
        std::erase(ready_, txid);
    }
    orphans_.erase(it);

    auto const at = std::ranges::find(order_, txid);
    *at = order_.back();
    order_.pop_back();
}

void tx_orphanage::erase_for_peer(uint64_t peer) {
    std::vector<hash_digest> sent;
    for (auto const& [txid, item] : orphans_) {
        if (item.peer == peer) {
            sent.push_back(txid);
        }
    }
    for (auto const& txid : sent) {
        erase(txid);
    }
}

size_t tx_orphanage::erase_for_block(domain::chain::block const& block) {
    std::vector<hash_digest> resolved;
    for (auto const& tx : block.transactions()) {
        for (auto const& input : tx.inputs()) {
            auto const& spent = input.previous_output();
            auto const parent = by_parent_.find(spent.hash());
            if (parent == by_parent_.end()) {
                continue;
            }
            for (auto const& child : parent->second) {
                auto const& inputs = orphans_.at(child).tx->inputs();
                auto const spends = std::ranges::any_of(inputs, [&](auto const& own) {
                    return own.previous_output().hash() == spent.hash()
                        && own.previous_output().index() == spent.index();
                });
                if (spends) {
                    resolved.push_back(child);
                }
            }
        }
    }
    std::ranges::sort(resolved);
    resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
    for (auto const& txid : resolved) {
        erase(txid);
    }
    return resolved.size();
}

void tx_orphanage::sweep(time_point now) {
    if (next_sweep_ > now) {
        return;
    }
    // As BCHN: the next sweep is sweep_interval after the earliest expiry
    // left, to batch the scan.
    auto earliest = now + expire_time - sweep_interval;
    std::vector<hash_digest> expired;
    for (auto const& [txid, item] : orphans_) {
        if (item.expires <= now) {
            expired.push_back(txid);
        } else {
            earliest = std::min(earliest, item.expires);
        }
    }
    for (auto const& txid : expired) {
        erase(txid);
    }
    next_sweep_ = earliest + sweep_interval;
}

// =============================================================================
// The channels
// =============================================================================

tx_relay_channels::tx_relay_channels()
    : commands(command_capacity)
    , tip_events(1)
{}

bool tx_relay_channels::submit(tx_relay_command command) {
    return commands.try_send(std::move(command));
}

void tx_relay_channels::tip_moved() {
    (void)tip_events.try_send(std::monostate{});
}

void tx_relay_channels::close() {
    commands.close();
    tip_events.close();
}

namespace {

// The ids and transactions a command carries, for the backlog's bounds.
size_t txids_in(tx_relay_command const& command) {
    if (auto const* announced = std::get_if<tx_announced>(&command)) {
        return announced->txids.size();
    }
    if (auto const* missing = std::get_if<tx_not_found>(&command)) {
        return missing->txids.size();
    }
    return 0;
}

size_t transactions_in(tx_relay_command const& command) {
    return std::holds_alternative<tx_received>(command) ? 1 : 0;
}

} // namespace

bool tx_relay_backlog::submit(tx_relay_channels& channels, tx_relay_command command) {
    flush(channels);
    if (pending_.empty() && channels.commands.try_send(tx_relay_command(command))) {
        return true;
    }
    return keep(std::move(command));
}

void tx_relay_backlog::flush(tx_relay_channels& channels) {
    while ( ! pending_.empty() && channels.commands.try_send(tx_relay_command(pending_.front()))) {
        txids_ -= txids_in(pending_.front());
        transactions_ -= transactions_in(pending_.front());
        pending_.pop_front();
    }
}

bool tx_relay_backlog::keep(tx_relay_command command) {
    auto const txids = txids_in(command);
    auto const transactions = transactions_in(command);
    if (txids_ + txids > max_txids || transactions_ + transactions > max_transactions) {
        return false;
    }
    txids_ += txids;
    transactions_ += transactions;
    pending_.push_back(std::move(command));
    return true;
}

namespace {

// Into the owner, waiting for room: what only the owner can undo — a peer
// marked busy, a transaction marked held — must reach it. False once closed.
::asio::awaitable<bool> report(tx_relay_channels& channels, tx_relay_command command) {
    auto const executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(channels.commands, std::move(command))
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, sent] = completion;
    (void)sent;
    co_return ! ec;
}

// Whether validation did not find inputs: an orphan.
bool missing_inputs(code const& result) {
    return result == error::missing_previous_output || result == error::orphan_transaction;
}

// Whether validation's answer means the transaction can never be valid on this
// tip. Missing inputs is not that: its parent may arrive next. Nor is an
// admission that raced a chain transition twice (stale_chain), or one that
// was cut short: neither judged the transaction.
bool rejected(code const& result) {
    return result
        && ! missing_inputs(result)
        && result != error::stale_chain
        && result != error::service_stopped
        && result != code(::asio::error::operation_aborted);
}

// The getdata for `txids`, in messages of at most max_getdata entries (BCHN
// MAX_GETDATA_SZ).
constexpr size_t max_getdata = 1000;

// One getdata to one peer, waiting for room in its queue as long as it takes,
// then the outcome to the owner. Everything by value: this outlives the call
// that started it.
::asio::awaitable<void> send_getdata(network::peer_session::ptr session,
                                     domain::message::get_data message, uint64_t peer,
                                     tx_relay_channels& channels) {
    auto const ec = co_await session->send(message);
    (void)co_await report(channels, tx_request_sent{peer, ec == error::success});
}

// How far back the scan steps along the branch it had seen to rejoin the
// connected chain after a reorganization. Past it, what the blocks in between
// would have resolved goes by expiry.
constexpr size_t max_scan_rewind = 100;

// The connected blocks after `from`, up to `to`, handed to the owner in order,
// then where the scan got to. A block whose parent is not the last one seen,
// or a tip at or below it, means the chain reorganized there: the scan steps
// back along what it had seen until it rejoins. A block that cannot be read
// ends the scan where it is; the next ring reads on from there.
::asio::awaitable<void> scan_connected(
    std::function<::asio::awaitable<domain::message::block::const_ptr>(size_t)> block_at,
    std::function<::asio::awaitable<std::optional<chain_point>>(hash_digest)> parent_of,
    chain_point from, chain_point to, tx_relay_channels& channels) {
    auto reached = from;
    size_t rewound = 0;
    auto const rewind = [&]() -> ::asio::awaitable<bool> {
        if (rewound == max_scan_rewind) {
            co_return false;
        }
        auto const parent = co_await parent_of(reached.hash);
        if ( ! parent) {
            co_return false;
        }
        reached = *parent;
        ++rewound;
        co_return true;
    };

    while (reached != to) {
        if (reached.height >= to.height) {
            if ( ! co_await rewind()) {
                reached = to;
            }
            continue;
        }
        auto const height = reached.height + 1;
        auto const block = co_await block_at(height);
        if ( ! block) {
            break;
        }
        if (block->header().previous_block_hash() != reached.hash) {
            if ( ! co_await rewind()) {
                reached = to;
            }
            continue;
        }
        chain_point const point{block->hash(), height};
        if ( ! co_await report(channels, tx_block_connected{block, point})) {
            co_return;
        }
        reached = point;
    }
    (void)co_await report(channels, tx_scan_ended{reached});
}

} // namespace

// =============================================================================
// The owner
// =============================================================================

namespace {

struct relay_peer {
    network::peer_session::ptr session;
    // A getdata to this peer is on its way. What follows it waits in the
    // tracker (take_unsent), not here.
    bool sending{false};
};

class relay_owner {
public:
    relay_owner(tx_relay_channels& channels, tx_relay_view view,
                tx_admission_channel& admission, tx_relay_spawner spawn)
        : channels_(channels)
        , view_(std::move(view))
        , admission_(admission)
        , spawn_(std::move(spawn))
    {
        if (view_.connected_tip) {
            seen_ = view_.connected_tip();
        }
    }

    std::optional<tx_request_tracker::time_point> next_wakeup() const {
        return tracker_.next_wakeup();
    }

    // The connected tip moved: what it resolves is read from the blocks
    // connected since the last one seen, by a task of its own. With no orphans,
    // or before synchronization, there is nothing to resolve and the mark only
    // moves. One scan at a time: a ring during it scans again after it.
    void tip_moved() {
        if ( ! view_.connected_tip) {
            return;
        }
        if (scanning_) {
            rescan_ = true;
            return;
        }
        auto const tip = view_.connected_tip();
        if ( ! tip) {
            return;
        }
        if ( ! seen_ || orphans_.size() == 0 || ! view_.synchronized()) {
            seen_ = tip;
            return;
        }
        if (*seen_ == *tip) {
            return;
        }
        scanning_ = true;
        spawn_(scan_connected(view_.block_at, view_.parent_of, *seen_, *tip, channels_));
    }

    void handle(tx_relay_command command, tx_request_tracker::time_point now) {
        rejects_.reset_for(view_.tip());
        std::visit([&](auto& value) { on(value, now); }, command);
    }

    // Ask for whatever is due. A request for a peer the owner no longer
    // knows fails over at once, which can make more due now: hence the loop.
    void dispatch(tx_request_tracker::time_point now) {
        bool again = true;
        while (again) {
            again = false;
            for (auto const& request : tracker_.take_requests(now).asked) {
                auto const it = peers_.find(request.peer);
                if (it == peers_.end()) {
                    tracker_.disconnected(request.peer);
                    again = true;
                    continue;
                }
                send_next(request.peer, it->second);
            }
        }
    }

private:
    void on(tx_announced& value, tx_request_tracker::time_point now) {
        if ( ! view_.synchronized()) {
            return;
        }
        auto const peer = value.peer->nonce();
        remember(value.peer);
        auto const preferred = value.peer->is_preferred_download();
        for (auto const& txid : value.txids) {
            if (rejects_.contains(txid) || orphans_.contains(txid) || view_.in_mempool(txid)) {
                continue;
            }
            (void)tracker_.announced(peer, preferred, txid, now);
        }
    }

    void on(tx_not_found& value, tx_request_tracker::time_point) {
        for (auto const& txid : value.txids) {
            tracker_.not_found(value.peer, txid);
        }
    }

    void on(tx_received& value, tx_request_tracker::time_point) {
        auto const txid = value.tx->hash();
        tracker_.received(value.peer, txid);
        if ( ! view_.synchronized()) {
            return;
        }
        // Unannounced or not, the sender is who its orphan's parents are
        // asked of (BCHN, TX_MISSING_INPUTS, AddTxAnnouncement from pfrom).
        if (value.session) {
            remember(value.session);
        }
        if (rejects_.contains(txid) || orphans_.contains(txid) || view_.in_mempool(txid)) {
            tracker_.forget(txid);
            return;
        }
        if (tracker_.held(txid)) {
            return;                                 // a copy is being validated
        }
        if (waiting_.empty() && admission_.try_send(tx_admission{value.tx, value.peer})) {
            tracker_.hold(txid);
            return;
        }
        if (waiting_.size() < tx_waiting_capacity) {
            // Validation is behind: kept, in order, for when it has room.
            waiting_.push_back({value.tx, value.peer});
            tracker_.hold(txid);
            return;
        }
        // Behind by more than is kept: the transaction is not, but its
        // announcement is, until validation has room again.
        if ( ! tracker_.park(value.peer, txid)) {
            spdlog::debug("[tx_relay] Transaction {} from peer {} dropped: validation is full "
                "and the peer is at its announcement bound", encode_hash(txid), value.peer);
        }
    }

    void on(tx_peer_gone& value, tx_request_tracker::time_point) {
        leave(value.peer);
    }

    void on(tx_request_sent& value, tx_request_tracker::time_point) {
        auto const it = peers_.find(value.peer);
        if (it == peers_.end()) {
            return;
        }
        it->second.sending = false;
        if ( ! value.sent) {
            // The session is stopping: what it was asked fails over now.
            leave(value.peer);
            return;
        }
        send_next(value.peer, it->second);
    }

    void on(tx_block_connected& value, tx_request_tracker::time_point) {
        auto const gone = orphans_.erase_for_block(*value.block);
        if (gone > 0) {
            spdlog::debug("[tx_relay] {} orphan(s) resolved by block {}", gone,
                encode_hash(value.point.hash));
        }
        seen_ = value.point;
    }

    void on(tx_scan_ended& value, tx_request_tracker::time_point) {
        seen_ = value.reached;
        scanning_ = false;
        if (rescan_) {
            rescan_ = false;
            tip_moved();
        }
    }

    void on(tx_admitted& value, tx_request_tracker::time_point now) {
        auto const txid = value.tx->hash();
        tracker_.forget(txid);
        if ( ! value.result) {
            orphans_.parent_admitted(txid);
        } else if (missing_inputs(value.result)) {
            orphan(value, now);
        } else if (rejected(value.result)) {
            rejects_.insert(txid);
        }
        feed(now);
    }

    // BCHN, TX_MISSING_INPUTS: rejected with a rejected parent; otherwise its
    // unknown parents are asked of the peer that sent it, and it is kept.
    void orphan(tx_admitted& value, tx_request_tracker::time_point now) {
        auto const txid = value.tx->hash();
        auto parents = std::move(value.missing_parents);
        std::ranges::sort(parents);
        parents.erase(std::unique(parents.begin(), parents.end()), parents.end());

        if (std::ranges::any_of(parents, [this](hash_digest const& parent) {
                return rejects_.contains(parent);
            })) {
            rejects_.insert(txid);
            return;
        }

        // A peer the owner does not know (it sent without announcing) cannot
        // be asked: the parents come from whoever announces them.
        auto const peer = peers_.find(value.peer);
        if (peer != peers_.end()) {
            auto const preferred = peer->second.session->is_preferred_download();
            for (auto const& parent : parents) {
                if (orphans_.contains(parent) || tracker_.held(parent) || view_.in_mempool(parent)) {
                    continue;
                }
                (void)tracker_.announced(value.peer, preferred, parent, now);
            }
        }

        if ( ! orphans_.add(value.tx, value.peer, now)) {
            spdlog::debug("[tx_relay] Orphan {} from peer {} not kept", encode_hash(txid), value.peer);
        }
    }

    // Validation took one: what waits goes next, in order, then the orphans
    // whose parent arrived. Each is taken from its place only once validation
    // has it: what does not fit stays, and goes at the next answer.
    void feed(tx_request_tracker::time_point now) {
        while ( ! waiting_.empty() && admission_.try_send(tx_admission(waiting_.front()))) {
            waiting_.pop_front();
        }
        if (waiting_.empty()) {
            while (auto next = orphans_.next_ready()) {
                auto const txid = next->tx->hash();
                if ( ! admission_.try_send(std::move(*next))) {
                    break;
                }
                orphans_.erase(txid);
                tracker_.hold(txid);
            }
        }
        // And with room to keep what arrives, what was parked is asked again.
        if (waiting_.size() < tx_waiting_capacity && tracker_.parked() > 0) {
            tracker_.unpark(now);
        }
    }

    // A peer is gone: what it was asked passes on, and its orphans go.
    void leave(uint64_t peer) {
        tracker_.disconnected(peer);
        orphans_.erase_for_peer(peer);
        peers_.erase(peer);
    }

    // A session whose end was not delivered (the channel was full) goes when
    // the next one arrives: the peers held stay bounded by those connected.
    void remember(network::peer_session::ptr const& session) {
        auto const peer = session->nonce();
        if ( ! peers_.contains(peer)) {
            forget_stopped_peers();
            peers_[peer].session = session;
        }
    }

    void forget_stopped_peers() {
        std::vector<uint64_t> stopped;
        for (auto const& [nonce, state] : peers_) {
            if (state.session->stopped()) {
                stopped.push_back(nonce);
            }
        }
        for (auto const nonce : stopped) {
            leave(nonce);
        }
    }

    // One getdata at a time per peer, from a task of its own: the owner never
    // waits on a peer's queue.
    void send_next(uint64_t peer, relay_peer& state) {
        if (state.sending) {
            return;
        }
        auto batch = tracker_.take_unsent(peer, max_getdata);
        if (batch.empty()) {
            return;
        }

        auto request = domain::message::get_data::create(batch,
            domain::message::inventory_vector::type_id::transaction);
        if ( ! request) {
            spdlog::debug("[tx_relay] getdata for peer {} not built: {}", peer,
                request.error().message());
            for (auto const& txid : batch) {
                tracker_.not_found(peer, txid);
            }
            return;
        }

        state.sending = true;
        spawn_(send_getdata(state.session, std::move(*request), peer, channels_));
    }

    tx_relay_channels& channels_;
    tx_relay_view view_;
    tx_admission_channel& admission_;
    tx_relay_spawner spawn_;
    tx_request_tracker tracker_;
    recent_rejects rejects_;
    boost::unordered_flat_map<uint64_t, relay_peer> peers_;
    tx_orphanage orphans_;
    // Received, validation full: taken by it as it frees room. Bounded.
    std::deque<tx_admission> waiting_;
    // The last connected block whose orphans were resolved.
    std::optional<chain_point> seen_;
    bool scanning_{false};
    bool rescan_{false};
};

} // namespace

::asio::awaitable<void> tx_relay_task(
    tx_relay_channels& channels,
    tx_relay_view view,
    tx_admission_channel& admission,
    tx_relay_spawner spawn)
{
    spdlog::debug("[tx_relay] Task started");
    auto const executor = co_await ::asio::this_coro::executor;
    auto const view_now = [clock = view.now] {
        return clock ? clock() : tx_request_tracker::clock::now();
    };
    relay_owner owner(channels, std::move(view), admission, std::move(spawn));

    while (true) {
        auto const wake = owner.next_wakeup().value_or(tx_request_tracker::time_point::max());
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(channels.commands),
            ::asio::experimental::receive(channels.tip_events),
            ::asio::experimental::after(wake)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            break;                                  // closed: the node is stopping
        }

        auto const now = view_now();
        if (selected.index == 0) {
            owner.handle(std::move(*selected.value<0>()), now);
        } else if (selected.index == 1) {
            owner.tip_moved();
        }
        owner.dispatch(now);
    }

    // The admission task ends after the transaction in hand.
    admission.close();
    spdlog::info("[tx_relay] Task ended");
}

// =============================================================================
// The chain, as the owner reads it
// =============================================================================

tx_relay_view chain_relay_view(blockchain::block_chain& chain) {
    return {
        .synchronized = [&chain] { return chain.synchronization().synchronized(); },
        .in_mempool = [&chain](hash_digest const& txid) { return chain.mempool_ref().contains(txid); },
        .tip = [&chain] {
            auto const view = chain.chain_view();
            return view ? view->tip_hash : null_hash;
        },
        .connected_tip = [&chain]() -> std::optional<chain_point> {
            auto const view = chain.chain_view();
            if ( ! view) {
                return std::nullopt;
            }
            return chain_point{view->tip_hash, view->connected_tip_height};
        },
        .block_at = [&chain](size_t height) -> ::asio::awaitable<domain::message::block::const_ptr> {
            auto const read = co_await chain.fetch_block(height);
            co_return read ? read->first : nullptr;
        },
        .parent_of = [&chain](hash_digest hash) -> ::asio::awaitable<std::optional<chain_point>> {
            auto const read = co_await chain.fetch_block_header(hash);
            if ( ! read || read->second == 0) {
                co_return std::nullopt;
            }
            co_return chain_point{read->first->previous_block_hash(), read->second - 1};
        },
    };
}

// =============================================================================
// Admission
// =============================================================================

::asio::awaitable<void> tx_admission_task(
    tx_admission_channel& admission,
    tx_submit_fn submit,
    tx_relay_channels& channels)
{
    auto const executor = co_await ::asio::this_coro::executor;
    while (true) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(admission)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            break;
        }
        auto const [tx, peer] = std::move(*selected.value<0>());
        auto const result = co_await submit(tx);
        if (result) {
            spdlog::debug("[tx_relay] Transaction {} not admitted: {}",
                encode_hash(tx->hash()), result.message());
        }
        // What validation did not find is read off the inputs it populated.
        auto missing = missing_inputs(result) ? tx->missing_previous_transactions() : hash_list{};
        if ( ! co_await report(channels, tx_admitted{tx, peer, result, std::move(missing)})) {
            break;
        }
    }
    spdlog::debug("[tx_relay] Admission task ended");
}

} // namespace kth::node::detail
