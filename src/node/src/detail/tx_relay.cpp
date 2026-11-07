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
// The channels
// =============================================================================

tx_relay_channels::tx_relay_channels()
    : commands(command_capacity)
{}

bool tx_relay_channels::submit(tx_relay_command command) {
    return commands.try_send(std::move(command));
}

void tx_relay_channels::close() {
    commands.close();
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

// Whether validation's answer means the transaction can never be valid on this
// tip. Missing inputs is not that: its parent may arrive next. Nor is an
// admission that raced a chain transition twice (stale_chain), or one that
// was cut short: neither judged the transaction.
bool rejected(code const& result) {
    return result
        && result != error::missing_previous_output
        && result != error::orphan_transaction
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
    {}

    std::optional<tx_request_tracker::time_point> next_wakeup() const {
        return tracker_.next_wakeup();
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
        if ( ! peers_.contains(peer)) {
            // A session whose end was not delivered (the channel was full)
            // goes when the next one arrives: the peers held stay bounded by
            // those connected.
            forget_stopped_peers();
            peers_[peer].session = value.peer;
        }
        auto const preferred = value.peer->is_preferred_download();
        for (auto const& txid : value.txids) {
            if (rejects_.contains(txid) || view_.in_mempool(txid)) {
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
        if (rejects_.contains(txid) || view_.in_mempool(txid)) {
            tracker_.forget(txid);
            return;
        }
        if (tracker_.held(txid)) {
            return;                                 // a copy is being validated
        }
        if (waiting_.empty() && admission_.try_send(tx_admission{value.tx})) {
            tracker_.hold(txid);
            return;
        }
        if (waiting_.size() < tx_waiting_capacity) {
            // Validation is behind: kept, in order, for when it has room.
            waiting_.push_back(value.tx);
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
        tracker_.disconnected(value.peer);
        peers_.erase(value.peer);
    }

    void on(tx_request_sent& value, tx_request_tracker::time_point) {
        auto const it = peers_.find(value.peer);
        if (it == peers_.end()) {
            return;
        }
        it->second.sending = false;
        if ( ! value.sent) {
            // The session is stopping: what it was asked fails over now.
            tracker_.disconnected(value.peer);
            peers_.erase(it);
            return;
        }
        send_next(value.peer, it->second);
    }

    void on(tx_admitted& value, tx_request_tracker::time_point now) {
        if (rejected(value.result)) {
            rejects_.insert(value.txid);
        }
        tracker_.forget(value.txid);

        // Validation took one: what waits goes next, in order.
        while ( ! waiting_.empty() && admission_.try_send(tx_admission{waiting_.front()})) {
            waiting_.pop_front();
        }
        // And with room to keep what arrives, what was parked is asked again.
        if (waiting_.size() < tx_waiting_capacity && tracker_.parked() > 0) {
            tracker_.unpark(now);
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
            tracker_.disconnected(nonce);
            peers_.erase(nonce);
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
    // Received, validation full: taken by it as it frees room. Bounded.
    std::deque<domain::message::transaction::const_ptr> waiting_;
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
            ::asio::experimental::after(wake)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            break;                                  // closed: the node is stopping
        }

        auto const now = view_now();
        if (selected.index == 0) {
            owner.handle(std::move(*selected.value<0>()), now);
        }
        owner.dispatch(now);
    }

    // The admission task ends after the transaction in hand.
    admission.close();
    spdlog::info("[tx_relay] Task ended");
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
        auto const tx = std::move(selected.value<0>()->tx);
        auto const result = co_await submit(tx);
        if (result) {
            spdlog::debug("[tx_relay] Transaction {} not admitted: {}",
                encode_hash(tx->hash()), result.message());
        }
        if ( ! co_await report(channels, tx_admitted{tx->hash(), result})) {
            break;
        }
    }
    spdlog::debug("[tx_relay] Admission task ended");
}

} // namespace kth::node::detail
