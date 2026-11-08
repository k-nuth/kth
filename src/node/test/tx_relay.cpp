// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/domain/message/fee_filter.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/not_found.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/node/detail/block_announcements.hpp>
#include <kth/node/detail/tx_relay.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/network/settings.hpp>

#include "header_peer_harness.hpp"
#include "tx_relay_harness.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::detail;
using namespace kth::test;
using namespace std::chrono_literals;

namespace {

using tracker = tx_request_tracker;

hash_digest txid_of(uint32_t tag) {
    hash_digest out = null_hash;
    std::memcpy(out.data(), &tag, sizeof(tag));
    out[31] = 0x7e;
    return out;
}

tracker::time_point const t0 = tracker::time_point{} + 1h;

// Whom each request goes to, as {peer, txids}, sorted by peer.
std::vector<tracker::request> due(tracker& requests, tracker::time_point now) {
    auto out = requests.take_requests(now).asked;
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.peer < b.peer; });
    return out;
}

bool asked(std::vector<tracker::request> const& requests, uint64_t peer, hash_digest const& txid) {
    for (auto const& request : requests) {
        if (request.peer == peer
            && std::find(request.txids.begin(), request.txids.end(), txid) != request.txids.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

// -----------------------------------------------------------------------------
// The tracker, as state
// -----------------------------------------------------------------------------

TEST_CASE("tx request tracker: one announcer at a time is asked", "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(1);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(requests.announced(2, true, tx, t0));

    auto const first = due(requests, t0);
    REQUIRE(first.size() == 1u);
    CHECK(asked(first, 1, tx));
    CHECK(requests.in_flight(1) == 1u);

    // While that request is out, nobody else is asked.
    CHECK(due(requests, t0 + 59s).empty());
    CHECK(requests.in_flight(2) == 0u);
}

TEST_CASE("tx request tracker: an unanswered request passes to the next announcer at its deadline",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(2);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(requests.announced(2, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));

    REQUIRE(requests.next_wakeup() == t0 + tracker::request_interval);
    CHECK(due(requests, t0 + tracker::request_interval - 1ms).empty());
    auto const next = due(requests, t0 + tracker::request_interval);
    CHECK(asked(next, 2, tx));
    CHECK(requests.in_flight(1) == 0u);
    CHECK(requests.in_flight(2) == 1u);
}

TEST_CASE("tx request tracker: a notfound passes it on at once", "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(3);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(requests.announced(2, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));

    requests.not_found(1, tx);
    CHECK(requests.in_flight(1) == 0u);
    CHECK(asked(due(requests, t0 + 1ms), 2, tx));
}

TEST_CASE("tx request tracker: a peer that leaves passes on what it was asked",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(4);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(requests.announced(2, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));

    requests.disconnected(1);
    CHECK(requests.announcements(1) == 0u);
    CHECK(asked(due(requests, t0 + 1ms), 2, tx));
}

TEST_CASE("tx request tracker: when every announcer failed, the transaction is forgotten",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(5);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));
    requests.not_found(1, tx);

    CHECK_FALSE(requests.tracking(tx));
    CHECK(requests.announcements(1) == 0u);
    CHECK_FALSE(requests.next_wakeup().has_value());

    // A later announcement starts over.
    REQUIRE(requests.announced(1, true, tx, t0 + 1s));
    CHECK(asked(due(requests, t0 + 1s), 1, tx));
}

TEST_CASE("tx request tracker: outbound peers are asked first, the others after a delay",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(6);
    REQUIRE(requests.announced(1, false, tx, t0));
    CHECK(due(requests, t0).empty());
    REQUIRE(requests.next_wakeup() == t0 + tracker::nonpreferred_delay);

    // An outbound announcement made later is still due first.
    REQUIRE(requests.announced(2, true, tx, t0 + 1s));
    CHECK(asked(due(requests, t0 + 1s), 2, tx));

    // And on its own, an inbound announcer is asked once its delay passed.
    auto const other = txid_of(7);
    REQUIRE(requests.announced(3, false, other, t0));
    CHECK_FALSE(asked(due(requests, t0 + 1s), 3, other));
    CHECK(asked(due(requests, t0 + tracker::nonpreferred_delay), 3, other));
}

TEST_CASE("tx request tracker: an overloaded peer's announcements wait longer",
          "[tx_relay][tracker]") {
    tracker requests;
    for (uint32_t i = 0; i < tracker::max_peer_in_flight; ++i) {
        REQUIRE(requests.announced(1, true, txid_of(100 + i), t0));
    }
    REQUIRE(due(requests, t0).front().txids.size() == tracker::max_peer_in_flight);
    REQUIRE(requests.in_flight(1) == tracker::max_peer_in_flight);

    auto const tx = txid_of(99);
    REQUIRE(requests.announced(1, true, tx, t0));
    CHECK_FALSE(asked(due(requests, t0 + 1s), 1, tx));
    CHECK(asked(due(requests, t0 + tracker::overloaded_delay), 1, tx));
}

TEST_CASE("tx request tracker: a peer's announcements are bounded, and a repeat is not one more",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(8);
    REQUIRE(requests.announced(1, true, tx, t0));
    CHECK_FALSE(requests.announced(1, true, tx, t0));
    CHECK(requests.announcements(1) == 1u);

    for (uint32_t i = 1; i < tracker::max_peer_announcements; ++i) {
        REQUIRE(requests.announced(1, true, txid_of(1'000'000 + i), t0));
    }
    CHECK(requests.announcements(1) == tracker::max_peer_announcements);
    CHECK_FALSE(requests.announced(1, true, txid_of(9), t0));
    // Another peer is not affected.
    CHECK(requests.announced(2, true, txid_of(9), t0));
}

TEST_CASE("tx request tracker: a transaction being validated is asked of nobody until forgotten",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(10);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(requests.announced(2, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));

    requests.received(1, tx);
    requests.hold(tx);
    CHECK(requests.held(tx));
    CHECK(requests.in_flight(1) == 0u);
    CHECK(due(requests, t0 + 10min).empty());

    requests.forget(tx);
    CHECK_FALSE(requests.tracking(tx));
    CHECK(requests.announcements(2) == 0u);
    CHECK(due(requests, t0 + 20min).empty());
}

TEST_CASE("tx request tracker: what expires is reported, so what was not sent is not sent",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(11);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));
    CHECK(requests.requested_from(1, tx));

    auto const later = requests.take_requests(t0 + tracker::request_interval);
    REQUIRE(later.expired.size() == 1u);
    CHECK(later.expired[0].peer == 1u);
    CHECK(later.expired[0].txids == hash_list{tx});
    CHECK_FALSE(requests.requested_from(1, tx));
}

TEST_CASE("tx request tracker: what waits to be sent is only what is still asked",
          "[tx_relay][tracker]") {
    tracker requests;
    for (uint32_t i = 0; i < 4; ++i) {
        REQUIRE(requests.announced(1, true, txid_of(20 + i), t0));
    }
    REQUIRE(due(requests, t0).front().txids.size() == 4u);
    CHECK(requests.unsent(1) == 4u);

    requests.not_found(1, txid_of(20));
    requests.received(1, txid_of(21));
    requests.forget(txid_of(22));
    CHECK(requests.unsent(1) == 1u);
    CHECK(requests.take_unsent(1, 10) == hash_list{txid_of(23)});
    CHECK(requests.unsent(1) == 0u);

    // Taken counts as sent: its deadline still runs, nothing waits.
    (void)requests.take_requests(t0 + tracker::request_interval);
    CHECK(requests.unsent(1) == 0u);
    CHECK(requests.in_flight(1) == 0u);
}

TEST_CASE("tx request tracker: a parked delivery is kept and asked again when unparked",
          "[tx_relay][tracker]") {
    tracker requests;
    auto const tx = txid_of(12);
    REQUIRE(requests.announced(1, true, tx, t0));
    REQUIRE(asked(due(requests, t0), 1, tx));

    // Delivered, and nothing could take it. Its only announcer is kept.
    requests.received(1, tx);
    REQUIRE(requests.park(1, tx));
    CHECK(requests.tracking(tx));
    CHECK(requests.parked() == 1u);
    CHECK(requests.in_flight(1) == 0u);
    CHECK(due(requests, t0 + 1h).empty());

    requests.unpark(t0 + 1h);
    CHECK(requests.parked() == 0u);
    CHECK(asked(due(requests, t0 + 1h), 1, tx));
}

TEST_CASE("tx request tracker: an unannounced delivery parks as its sender's announcement, within its bound",
          "[tx_relay][tracker]") {
    tracker requests;
    REQUIRE(requests.park(1, txid_of(13)));
    CHECK(requests.announcements(1) == 1u);
    requests.unpark(t0);
    CHECK(asked(due(requests, t0), 1, txid_of(13)));

    for (uint32_t i = 1; i < tracker::max_peer_announcements; ++i) {
        REQUIRE(requests.announced(2, true, txid_of(2'000'000 + i), t0));
    }
    REQUIRE(requests.park(2, txid_of(14)));
    CHECK_FALSE(requests.park(2, txid_of(15)));
    CHECK_FALSE(requests.tracking(txid_of(15)));
}

TEST_CASE("recent rejects: kept until the tip moves, and bounded", "[tx_relay][rejects]") {
    recent_rejects rejects;
    rejects.reset_for(txid_of(0xaa));
    rejects.insert(txid_of(1));
    CHECK(rejects.contains(txid_of(1)));

    rejects.reset_for(txid_of(0xaa));
    CHECK(rejects.contains(txid_of(1)));

    rejects.reset_for(txid_of(0xbb));
    CHECK_FALSE(rejects.contains(txid_of(1)));

    for (uint32_t i = 0; i <= recent_rejects::capacity; ++i) {
        rejects.insert(txid_of(10 + i));
    }
    CHECK(rejects.size() == recent_rejects::capacity);
    CHECK_FALSE(rejects.contains(txid_of(10)));
    CHECK(rejects.contains(txid_of(11)));
}

// -----------------------------------------------------------------------------
// The owner, running
// -----------------------------------------------------------------------------

namespace {

domain::message::transaction::const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, locktime, domain::chain::input::list{}, domain::chain::output::list{}));
}

// The owner and the admission task over a chain that is only what the relay
// reads of it.
struct relay_rig {
    ::asio::io_context ioc;
    network::settings const config = relay_settings();
    tx_relay_channels channels;
    tx_admission_channel admission{tx_admission_capacity};

    bool synchronized = true;
    boost::unordered_flat_set<hash_digest> mempool;
    hash_digest tip = txid_of(0xaa);
    code verdict = error::success;
    // Per transaction, when set; `verdict` otherwise.
    std::function<code(domain::message::transaction const&)> judge;
    std::vector<domain::message::transaction::const_ptr> submitted;

    std::future<void> owner;
    std::future<void> admitter;

    std::function<tx_request_tracker::time_point()> clock;

    // The connected chain, by height, when a control gives the relay one;
    // every block ever connected under its parent, side branches included.
    std::vector<domain::message::block::const_ptr> blocks;
    boost::unordered_flat_map<hash_digest, chain_point> parents;
    boost::unordered_flat_set<size_t> unreadable;
    size_t reads = 0;
    // Called as each block is read, before it is returned.
    std::function<void(size_t)> reading;

    tx_relay_view view() {
        tx_relay_view result{
            .synchronized = [this] { return synchronized; },
            .in_mempool = [this](hash_digest const& txid) { return mempool.contains(txid); },
            .tip = [this] { return tip; },
            .now = clock,
        };
        if ( ! blocks.empty()) {
            result.connected_tip = [this]() -> std::optional<chain_point> {
                return chain_point{blocks.back()->hash(), blocks.size() - 1};
            };
            result.block_at = [this](size_t height) -> ::asio::awaitable<domain::message::block::const_ptr> {
                ++reads;
                if (reading) {
                    reading(height);
                }
                if (height >= blocks.size() || unreadable.contains(height)) {
                    co_return nullptr;
                }
                co_return blocks[height];
            };
            result.parent_of = [this](hash_digest hash) -> ::asio::awaitable<std::optional<chain_point>> {
                auto const it = parents.find(hash);
                if (it == parents.end()) {
                    co_return std::nullopt;
                }
                co_return it->second;
            };
        }
        return result;
    }

    void start(bool admitting = true) {
        owner = ::asio::co_spawn(ioc, tx_relay_task(channels, view(), admission,
            [this](::asio::awaitable<void> task) {
                ::asio::co_spawn(ioc, std::move(task), ::asio::detached);
            }), ::asio::use_future);
        if (admitting) {
            admitter = ::asio::co_spawn(ioc, tx_admission_task(admission,
                [this](domain::message::transaction::const_ptr tx) -> ::asio::awaitable<code> {
                    submitted.push_back(tx);
                    auto const result = judge ? judge(*tx) : verdict;
                    if ( ! result) {
                        mempool.insert(tx->hash());
                    }
                    co_return result;
                }, channels), ::asio::use_future);
        }
        drain(ioc);
    }

    void submit(tx_relay_command command) {
        REQUIRE(channels.submit(std::move(command)));
        drain(ioc);
    }

    bool ended(std::future<void> const& task) const {
        return task.wait_for(0s) == std::future_status::ready;
    }

    ~relay_rig() {
        channels.close();
        drain(ioc);
    }
};

} // namespace

TEST_CASE("tx relay: a peer that never delivers does not keep another from being asked",
          "[tx_relay][owner]") {
    // The first announcer is gone before its getdata leaves. The transaction is
    // not left marked as asked for: a live peer's announcement asks it.
    relay_rig rig;
    auto gone = make_peer(rig.ioc, rig.config, 1);
    make_preferred(gone);
    gone->stop();
    auto live = connect(rig.ioc, rig.config, 2);
    rig.start();

    auto const tx = make_tx(1);
    rig.submit(tx_announced{gone, {tx->hash()}});
    rig.submit(tx_announced{live.session, {tx->hash()}});
    CHECK(contains(read_requests(rig.ioc, live, 2s), tx->hash()));
}

TEST_CASE("tx relay: a getdata that cannot be sent passes the transaction on at once",
          "[tx_relay][owner]") {
    // Both peers are known to the owner already, so no newcomer prompts a look
    // at which sessions stopped: only the failed send says so.
    relay_rig rig;
    auto first = connect(rig.ioc, rig.config, 1);
    auto second = connect(rig.ioc, rig.config, 2);
    rig.start();
    rig.submit(tx_announced{first.session, {make_tx(20)->hash()}});
    rig.submit(tx_announced{second.session, {make_tx(21)->hash()}});

    first.session->stop();
    auto const tx = make_tx(22);
    rig.submit(tx_announced{first.session, {tx->hash()}});
    rig.submit(tx_announced{second.session, {tx->hash()}});
    CHECK(contains(read_requests(rig.ioc, second, 2s, 2), tx->hash()));
}

TEST_CASE("tx relay: what a disconnected peer was asked passes to the next announcer",
          "[tx_relay][owner]") {
    relay_rig rig;
    auto first = connect(rig.ioc, rig.config, 1);
    auto second = connect(rig.ioc, rig.config, 2);
    rig.start();

    auto const tx = make_tx(2);
    rig.submit(tx_announced{first.session, {tx->hash()}});
    rig.submit(tx_announced{second.session, {tx->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, first, 2s), tx->hash()));
    CHECK(read_requests(rig.ioc, second, 100ms).empty());

    rig.submit(tx_peer_gone{1});
    CHECK(contains(read_requests(rig.ioc, second, 2s), tx->hash()));
}

TEST_CASE("tx relay: a notfound passes the transaction to the next announcer",
          "[tx_relay][owner]") {
    relay_rig rig;
    auto first = connect(rig.ioc, rig.config, 1);
    auto second = connect(rig.ioc, rig.config, 2);
    rig.start();

    auto const tx = make_tx(3);
    rig.submit(tx_announced{first.session, {tx->hash()}});
    rig.submit(tx_announced{second.session, {tx->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, first, 2s), tx->hash()));

    rig.submit(tx_not_found{1, {tx->hash()}});
    CHECK(contains(read_requests(rig.ioc, second, 2s), tx->hash()));
}

TEST_CASE("tx relay: a peer whose queue is full does not hold up the others",
          "[tx_relay][owner]") {
    // The owner never waits on a peer: the getdata for the blocked one waits in
    // a task of its own, and the other is asked meanwhile.
    relay_rig rig;
    auto blocked = connect(rig.ioc, rig.config, 1);
    auto other = connect(rig.ioc, rig.config, 2);
    auto filled = ::asio::co_spawn(rig.ioc, [session = blocked.session]() -> ::asio::awaitable<void> {
        for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
            REQUIRE(co_await session->send(domain::message::ping{uint64_t(i)}) == error::success);
        }
    }, ::asio::use_future);
    drain(rig.ioc);
    REQUIRE(filled.wait_for(0s) == std::future_status::ready);
    rig.start();

    auto const stuck = make_tx(4);
    auto const flowing = make_tx(5);
    rig.submit(tx_announced{blocked.session, {stuck->hash()}});
    rig.submit(tx_announced{other.session, {flowing->hash()}});
    CHECK(contains(read_requests(rig.ioc, other, 2s), flowing->hash()));
}

TEST_CASE("tx relay: a delivered transaction is validated once and reaches the pool",
          "[tx_relay][owner]") {
    relay_rig rig;
    auto peer = connect(rig.ioc, rig.config, 1);
    auto copy = connect(rig.ioc, rig.config, 2);
    rig.start();

    auto const tx = make_tx(6);
    rig.submit(tx_announced{peer.session, {tx->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, peer, 2s), tx->hash()));
    rig.submit(tx_received{1, tx});
    rig.submit(tx_received{2, tx});             // a second copy, unasked
    REQUIRE(rig.submitted.size() == 1u);
    CHECK(rig.mempool.contains(tx->hash()));

    // In the pool now: announcing it asks for nothing.
    rig.submit(tx_announced{copy.session, {tx->hash()}});
    CHECK(read_requests(rig.ioc, copy, 200ms).empty());
}

TEST_CASE("tx relay: a rejected transaction is not asked for again until the tip moves",
          "[tx_relay][owner]") {
    relay_rig rig;
    rig.verdict = error::insufficient_fee;
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    auto const tx = make_tx(7);
    rig.submit(tx_received{1, tx});
    REQUIRE(rig.submitted.size() == 1u);

    rig.submit(tx_announced{peer.session, {tx->hash()}});
    CHECK(read_requests(rig.ioc, peer, 200ms).empty());

    rig.tip = txid_of(0xbb);
    rig.submit(tx_announced{peer.session, {tx->hash()}});
    CHECK(contains(read_requests(rig.ioc, peer, 2s), tx->hash()));
}

TEST_CASE("tx relay: what validation did not judge is not remembered as rejected",
          "[tx_relay][owner]") {
    // stale_chain: admission raced a chain transition twice. service_stopped:
    // it was cut short. Neither says the transaction is invalid. (Missing
    // inputs is not either; that one is kept as an orphan, below.)
    relay_rig rig;
    rig.verdict = GENERATE(code(error::stale_chain), code(error::service_stopped));
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    auto const tx = make_tx(8);
    rig.submit(tx_received{1, tx});
    REQUIRE(rig.submitted.size() == 1u);

    // Asked for again when announced.
    rig.submit(tx_announced{peer.session, {tx->hash()}});
    CHECK(contains(read_requests(rig.ioc, peer, 2s), tx->hash()));
}

TEST_CASE("tx relay: until the node is synchronized, nothing is asked for or validated",
          "[tx_relay][owner]") {
    relay_rig rig;
    rig.synchronized = false;
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    auto const tx = make_tx(9);
    rig.submit(tx_announced{peer.session, {tx->hash()}});
    rig.submit(tx_received{1, tx});
    CHECK(read_requests(rig.ioc, peer, 200ms).empty());
    CHECK(rig.submitted.empty());
}

namespace {

// Take one transaction from the admission channel, as the admission task
// would, and report it admitted.
hash_digest admit_one(relay_rig& rig) {
    std::error_code ec;
    auto const taken = rig.admission.try_receive(ec);
    REQUIRE(taken);
    rig.submit(tx_admitted{taken->tx, taken->peer, error::success});
    return taken->tx->hash();
}

bool admission_holds(relay_rig& rig, hash_digest const& txid) {
    std::vector<tx_admission> taken;
    std::error_code ec;
    bool found = false;
    while (auto item = rig.admission.try_receive(ec)) {
        found = found || item->tx->hash() == txid;
        taken.push_back(std::move(*item));
    }
    for (auto& item : taken) {
        REQUIRE(rig.admission.try_send(std::move(item)));
    }
    return found;
}

} // namespace

TEST_CASE("tx relay: a delivery validation cannot take yet waits, and is taken when it has room",
          "[tx_relay][owner][admission]") {
    // No admission task: the controls take from its channel themselves.
    relay_rig rig;
    rig.start(false);
    for (uint32_t i = 0; i < tx_admission_capacity; ++i) {
        rig.submit(tx_received{1, make_tx(300 + i)});
    }
    auto const late = make_tx(299);
    rig.submit(tx_received{1, late});
    REQUIRE_FALSE(admission_holds(rig, late->hash()));

    (void)admit_one(rig);
    CHECK(admission_holds(rig, late->hash()));
}

TEST_CASE("tx relay: a delivery kept for validation is asked of nobody, and validated when it has room",
          "[tx_relay][owner][admission]") {
    relay_rig rig;
    auto peer = connect(rig.ioc, rig.config, 1);
    auto other = connect(rig.ioc, rig.config, 2);
    rig.start(false);
    for (uint32_t i = 0; i < tx_admission_capacity; ++i) {
        rig.submit(tx_received{1, make_tx(100 + i)});
    }
    auto const extra = make_tx(99);
    rig.submit(tx_received{1, extra});

    // Kept, not dropped: announcing it asks for nothing, as for what
    // validation already took.
    rig.submit(tx_announced{other.session, {extra->hash()}});
    rig.submit(tx_announced{peer.session, {make_tx(100)->hash()}});
    CHECK(read_requests(rig.ioc, other, 200ms).empty());
    CHECK(read_requests(rig.ioc, peer, 200ms).empty());

    (void)admit_one(rig);
    CHECK(admission_holds(rig, extra->hash()));
}

TEST_CASE("tx relay: a delivery past what is kept is asked for again when validation has room",
          "[tx_relay][owner][admission]") {
    // Its only announcer delivered it while validation and the waiting room
    // were full. Not lost: asked again of that peer once there is room.
    relay_rig rig;
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start(false);
    for (uint32_t i = 0; i < tx_admission_capacity + tx_waiting_capacity; ++i) {
        rig.submit(tx_received{2, make_tx(1000 + i)});
    }

    auto const tx = make_tx(999);
    rig.submit(tx_announced{peer.session, {tx->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, peer, 2s), tx->hash()));
    rig.submit(tx_received{1, tx});
    CHECK(read_requests(rig.ioc, peer, 200ms, 2).size() == 1u);

    (void)admit_one(rig);
    auto const requests = read_requests(rig.ioc, peer, 2s, 2);
    REQUIRE(requests.size() == 2u);
    CHECK(std::find(requests[1].begin(), requests[1].end(), tx->hash()) != requests[1].end());
}

TEST_CASE("tx relay: what expires before its getdata leaves is not sent, and asked again once",
          "[tx_relay][owner][expiry]") {
    // The peer's queue is full, so the first getdata waits and what follows
    // queues behind it. Requests expire and are asked again three times;
    // when the queue drains, each transaction still asked goes out once.
    auto clock = tx_request_tracker::clock::now();
    relay_rig rig;
    rig.clock = [&clock] { return clock; };
    auto blocked = connect(rig.ioc, rig.config, 1);
    auto filled = ::asio::co_spawn(rig.ioc, [session = blocked.session]() -> ::asio::awaitable<void> {
        for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
            REQUIRE(co_await session->send(domain::message::ping{uint64_t(i)}) == error::success);
        }
    }, ::asio::use_future);
    drain(rig.ioc);
    REQUIRE(filled.wait_for(0s) == std::future_status::ready);
    rig.start();

    auto const first = make_tx(40);
    auto const second = make_tx(41);
    rig.submit(tx_announced{blocked.session, {first->hash()}});
    for (int cycle = 0; cycle < 3; ++cycle) {
        rig.submit(tx_announced{blocked.session, {second->hash()}});
        clock += tx_request_tracker::request_interval + 1s;
        rig.submit(tx_not_found{99, {}});        // anything: the owner looks at the clock
    }
    rig.submit(tx_announced{blocked.session, {second->hash()}});

    auto const sent = read_requests(rig.ioc, blocked, 2s, 2);
    size_t seconds = 0;
    for (auto const& request : sent) {
        seconds += size_t(std::count(request.begin(), request.end(), second->hash()));
    }
    CHECK(sent.size() == 2u);
    CHECK(seconds == 1u);
}

TEST_CASE("tx relay: what a peer answers with notfound before its getdata leaves is not kept",
          "[tx_relay][owner][expiry]") {
    // The peer's queue is full, so nothing asked of it leaves. It announces
    // and then disowns transaction after transaction: what waits for it to be
    // sent stays what it is still asked for, not everything it ever was.
    relay_rig rig;
    auto blocked = connect(rig.ioc, rig.config, 1);
    auto filled = ::asio::co_spawn(rig.ioc, [session = blocked.session]() -> ::asio::awaitable<void> {
        for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
            REQUIRE(co_await session->send(domain::message::ping{uint64_t(i)}) == error::success);
        }
    }, ::asio::use_future);
    drain(rig.ioc);
    REQUIRE(filled.wait_for(0s) == std::future_status::ready);
    rig.start();

    rig.submit(tx_announced{blocked.session, {make_tx(500)->hash()}});
    for (uint32_t i = 1; i <= 2000; ++i) {
        auto const txid = make_tx(500 + i)->hash();
        rig.submit(tx_announced{blocked.session, {txid}});
        rig.submit(tx_not_found{1, {txid}});
    }
    auto const last = make_tx(9999);
    rig.submit(tx_announced{blocked.session, {last->hash()}});

    // One getdata waits for room; after it, only the one still asked.
    auto const sent = read_requests(rig.ioc, blocked, 2s, 2);
    REQUIRE(sent.size() == 2u);
    CHECK(sent[1] == hash_list{last->hash()});
}

// -----------------------------------------------------------------------------
// Orphans
// -----------------------------------------------------------------------------

namespace {

// A transaction spending output 0 of each of `parents`.
domain::message::transaction::const_ptr make_child(uint32_t locktime, hash_list const& parents) {
    domain::chain::input::list inputs;
    for (auto const& parent : parents) {
        inputs.emplace_back(domain::chain::output_point{parent, 0}, domain::chain::script{}, 0);
    }
    return std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, locktime, std::move(inputs), domain::chain::output::list{}));
}

// A pool that knows nothing of the chain: inputs are found only in it.
code by_pool(relay_rig const& rig, domain::message::transaction const& tx) {
    for (auto const& input : tx.inputs()) {
        if ( ! rig.mempool.contains(input.previous_output().hash())) {
            return error::missing_previous_output;
        }
    }
    return error::success;
}

tx_orphanage::time_point const o0 = tx_orphanage::time_point{} + 1h;

} // namespace

TEST_CASE("tx orphanage: kept once, not when too large, and found by its parent",
          "[tx_relay][orphans]") {
    tx_orphanage orphans;
    auto const parent = make_tx(1)->hash();
    auto const child = make_child(2, {parent});
    REQUIRE(orphans.add(child, 1, o0));
    CHECK(orphans.contains(child->hash()));
    CHECK_FALSE(orphans.add(child, 2, o0));
    CHECK_FALSE(orphans.next_ready());

    domain::chain::output::list outputs;
    outputs.emplace_back(0, domain::chain::script(data_chunk(tx_orphanage::max_transaction_size, 0x6a), false), std::nullopt);
    auto const large = std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, 3, domain::chain::input::list{
            domain::chain::input(domain::chain::output_point{parent, 1}, domain::chain::script{}, 0)},
            std::move(outputs)));
    CHECK_FALSE(orphans.add(large, 1, o0));
    CHECK(orphans.size() == 1u);

    orphans.parent_admitted(make_tx(9)->hash());
    CHECK(orphans.ready() == 0u);
    orphans.parent_admitted(parent);
    auto const ready = orphans.next_ready();
    REQUIRE(ready);
    CHECK(ready->tx->hash() == child->hash());
    CHECK(ready->peer == 1u);
    orphans.erase(child->hash());
    CHECK(orphans.size() == 0u);
    CHECK_FALSE(orphans.next_ready());
}

TEST_CASE("tx orphanage: bounded, what expired is swept, and a peer's go with it",
          "[tx_relay][orphans]") {
    // The pick is the controls': always the first kept.
    tx_orphanage orphans([](size_t) { return size_t{0}; });
    auto const parent = make_tx(1)->hash();
    for (uint32_t i = 0; i < tx_orphanage::capacity; ++i) {
        REQUIRE(orphans.add(make_child(100 + i, {parent}), 1 + i % 2, o0));
    }
    REQUIRE(orphans.size() == tx_orphanage::capacity);
    REQUIRE(orphans.add(make_child(99, {parent}), 3, o0));
    CHECK(orphans.size() == tx_orphanage::capacity);
    CHECK_FALSE(orphans.contains(make_child(100, {parent})->hash()));

    orphans.erase_for_peer(2);
    CHECK(orphans.size() == tx_orphanage::capacity / 2);
    CHECK_FALSE(orphans.contains(make_child(101, {parent})->hash()));

    // Not yet expired: nothing swept.
    REQUIRE(orphans.add(make_child(98, {parent}), 3, o0 + tx_orphanage::expire_time - 1s));
    CHECK(orphans.size() == tx_orphanage::capacity / 2 + 1);
    // Expired, and a sweep is due: only the late one is left.
    REQUIRE(orphans.add(make_child(97, {parent}), 3, o0 + tx_orphanage::expire_time));
    CHECK(orphans.size() == 2u);
    CHECK(orphans.contains(make_child(98, {parent})->hash()));
}

TEST_CASE("tx relay: an orphan's parent is asked of its sender, and the orphan validated again when it arrives",
          "[tx_relay][owner][orphans]") {
    relay_rig rig;
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    auto const parent = make_tx(40);
    auto const child = make_child(41, {parent->hash()});
    rig.submit(tx_announced{peer.session, {child->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, peer, 2s), child->hash()));
    rig.submit(tx_received{1, child});
    REQUIRE(rig.submitted.size() == 1u);

    auto const requests = read_requests(rig.ioc, peer, 2s, 2);
    REQUIRE(requests.size() == 2u);
    CHECK(requests[1] == hash_list{parent->hash()});

    rig.submit(tx_received{1, parent});
    REQUIRE(rig.submitted.size() == 3u);
    CHECK(rig.submitted[2]->hash() == child->hash());
    CHECK(rig.mempool.contains(child->hash()));
}

TEST_CASE("tx relay: an orphan sent without an announcement has its parents asked of its sender",
          "[tx_relay][owner][orphans]") {
    relay_rig rig;
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    // Nothing announced: the peer just sends it.
    auto const parent = make_tx(50);
    auto const child = make_child(51, {parent->hash()});
    rig.submit(tx_received{1, child, peer.session});
    REQUIRE(rig.submitted.size() == 1u);

    auto const requests = read_requests(rig.ioc, peer, 2s);
    REQUIRE(requests.size() == 1u);
    CHECK(requests[0] == hash_list{parent->hash()});

    rig.submit(tx_received{1, parent, peer.session});
    REQUIRE(rig.submitted.size() == 3u);
    CHECK(rig.submitted[2]->hash() == child->hash());
    CHECK(rig.mempool.contains(child->hash()));
}

TEST_CASE("tx relay: a kept orphan is not asked for again",
          "[tx_relay][owner][orphans]") {
    relay_rig rig;
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    auto peer = connect(rig.ioc, rig.config, 1);
    auto other = connect(rig.ioc, rig.config, 2);
    rig.start();

    auto const child = make_child(42, {make_tx(43)->hash()});
    rig.submit(tx_received{1, child});
    rig.submit(tx_announced{other.session, {child->hash()}});
    rig.submit(tx_received{2, child});
    CHECK(read_requests(rig.ioc, other, 200ms).empty());
    CHECK(rig.submitted.size() == 1u);
}

TEST_CASE("tx relay: an orphan of a rejected transaction is rejected",
          "[tx_relay][owner][orphans]") {
    relay_rig rig;
    auto const parent = make_tx(44);
    rig.judge = [&rig, &parent](domain::message::transaction const& tx) {
        return tx.hash() == parent->hash() ? code(error::insufficient_fee) : by_pool(rig, tx);
    };
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    rig.submit(tx_received{1, parent});
    auto const child = make_child(45, {parent->hash()});
    rig.submit(tx_announced{peer.session, {child->hash()}});
    REQUIRE(contains(read_requests(rig.ioc, peer, 2s), child->hash()));
    rig.submit(tx_received{1, child});
    REQUIRE(rig.submitted.size() == 2u);

    // Not kept, and not asked for again on this tip.
    rig.submit(tx_announced{peer.session, {child->hash()}});
    CHECK(read_requests(rig.ioc, peer, 200ms, 2).size() == 1u);
}

TEST_CASE("tx relay: a peer's orphans go when it does",
          "[tx_relay][owner][orphans]") {
    relay_rig rig;
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    auto peer = connect(rig.ioc, rig.config, 1);
    rig.start();

    auto const parent = make_tx(46);
    auto const child = make_child(47, {parent->hash()});
    rig.submit(tx_received{1, child});
    rig.submit(tx_peer_gone{1});
    rig.submit(tx_received{2, parent});
    REQUIRE(rig.mempool.contains(parent->hash()));
    CHECK(rig.submitted.size() == 2u);
    CHECK_FALSE(rig.mempool.contains(child->hash()));
}

TEST_CASE("tx relay: an orphan ready while validation is full is validated when it has room",
          "[tx_relay][owner][orphans][admission]") {
    // No admission task: the controls take from its channel and answer.
    relay_rig rig;
    rig.start(false);
    auto const parent = make_tx(48);
    auto const child = make_child(49, {parent->hash()});
    rig.submit(tx_received{1, child});
    std::error_code ec;
    auto const taken = rig.admission.try_receive(ec);
    REQUIRE(taken);
    rig.submit(tx_admitted{taken->tx, 1, error::missing_previous_output, {parent->hash()}});

    for (uint32_t i = 0; i < tx_admission_capacity; ++i) {
        rig.submit(tx_received{2, make_tx(2000 + i)});
    }
    rig.submit(tx_admitted{parent, 2, error::success});
    REQUIRE_FALSE(admission_holds(rig, child->hash()));

    (void)admit_one(rig);
    CHECK(admission_holds(rig, child->hash()));
}

// -----------------------------------------------------------------------------
// Orphans a connected block resolves
// -----------------------------------------------------------------------------

namespace {

// A transaction spending output `index` of `parent`.
domain::message::transaction::const_ptr make_spend(uint32_t locktime, hash_digest const& parent,
                                                   uint32_t index) {
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{parent, index}, domain::chain::script{}, 0);
    return std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, locktime, std::move(inputs), domain::chain::output::list{}));
}

domain::message::block::const_ptr make_block(hash_digest const& previous, uint32_t nonce,
    std::vector<domain::message::transaction::const_ptr> const& transactions) {
    domain::chain::transaction::list list;
    for (auto const& tx : transactions) {
        list.push_back(*tx);
    }
    return std::make_shared<domain::message::block const>(
        domain::chain::header(1, previous, null_hash, 0, 0, nonce), std::move(list));
}

// A chain of one block, before the relay starts.
void genesis(relay_rig& rig) {
    rig.blocks = {make_block(null_hash, 0, {})};
}

// One block more on the connected chain.
void extend(relay_rig& rig, uint32_t nonce,
            std::vector<domain::message::transaction::const_ptr> const& transactions = {}) {
    auto const& tip = rig.blocks.back();
    auto block = make_block(tip->hash(), nonce, transactions);
    rig.parents[block->hash()] = chain_point{tip->hash(), rig.blocks.size() - 1};
    rig.blocks.push_back(std::move(block));
}

void ring(relay_rig& rig) {
    rig.channels.tip_moved();
    drain(rig.ioc);
}

// Whether `child` is still kept: its parent arrives, and a kept orphan is
// validated again.
bool kept_until_parent(relay_rig& rig, domain::message::transaction::const_ptr const& parent,
                       hash_digest const& child) {
    auto const before = rig.submitted.size();
    rig.submit(tx_received{9, parent});
    return std::any_of(rig.submitted.begin() + std::ptrdiff_t(before), rig.submitted.end(),
        [&](auto const& tx) { return tx->hash() == child; });
}

} // namespace

TEST_CASE("tx orphanage: what spends an output a block spends goes, the block's own among them",
          "[tx_relay][orphans][block]") {
    tx_orphanage orphans;
    auto const parent = make_tx(60)->hash();
    auto const first = make_spend(61, parent, 0);
    auto const second = make_spend(62, parent, 1);
    auto const third = make_spend(63, parent, 2);
    REQUIRE(orphans.add(first, 1, o0));
    REQUIRE(orphans.add(second, 1, o0));
    REQUIRE(orphans.add(third, 1, o0));

    // Another spend of the same parent, of another output: none goes.
    CHECK(orphans.erase_for_block(*make_block(null_hash, 1, {make_spend(64, parent, 7)})) == 0u);
    CHECK(orphans.size() == 3u);

    // A conflict: what spends the same output goes, what spends another stays.
    CHECK(orphans.erase_for_block(*make_block(null_hash, 2, {make_spend(65, parent, 0)})) == 1u);
    CHECK_FALSE(orphans.contains(first->hash()));
    CHECK(orphans.contains(second->hash()));

    // Confirmed: the orphan itself is in the block.
    CHECK(orphans.erase_for_block(*make_block(null_hash, 3, {second})) == 1u);
    CHECK_FALSE(orphans.contains(second->hash()));
    CHECK(orphans.contains(third->hash()));

    // Gone from every index: its parent arriving readies only what is kept.
    orphans.parent_admitted(parent);
    CHECK(orphans.ready() == 1u);
    REQUIRE(orphans.next_ready());
    CHECK(orphans.next_ready()->tx->hash() == third->hash());
}

TEST_CASE("tx relay: an orphan a connected block resolves goes, one it does not stays",
          "[tx_relay][owner][orphans][block]") {
    relay_rig rig;
    genesis(rig);
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    rig.start();

    auto const parent = make_tx(70);
    auto const resolved = make_spend(71, parent->hash(), 0);
    auto const unrelated = make_spend(72, parent->hash(), 1);
    rig.submit(tx_received{1, resolved});
    rig.submit(tx_received{1, unrelated});
    REQUIRE(rig.submitted.size() == 2u);

    extend(rig, 1, {make_spend(73, parent->hash(), 0)});
    ring(rig);
    CHECK(rig.reads == 1u);

    auto const before = rig.submitted.size();
    rig.submit(tx_received{9, parent});
    std::vector<hash_digest> again;
    for (auto i = before; i < rig.submitted.size(); ++i) {
        again.push_back(rig.submitted[i]->hash());
    }
    CHECK(again == std::vector<hash_digest>{parent->hash(), unrelated->hash()});
}

TEST_CASE("tx relay: with no orphans no block is read, and what was connected before one is not",
          "[tx_relay][owner][orphans][block]") {
    relay_rig rig;
    genesis(rig);
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    rig.start();

    auto const parent = make_tx(80);
    auto const child = make_spend(81, parent->hash(), 0);
    // Blocks before the orphan, one of them spending what it will spend.
    extend(rig, 1, {make_spend(82, parent->hash(), 0)});
    extend(rig, 2);
    ring(rig);
    CHECK(rig.reads == 0u);

    rig.submit(tx_received{1, child});
    extend(rig, 3);
    ring(rig);
    // Only the block connected since: the orphan is kept.
    CHECK(rig.reads == 1u);
    CHECK(kept_until_parent(rig, parent, child->hash()));
}

TEST_CASE("tx relay: rings during a scan are not lost",
          "[tx_relay][owner][orphans][block]") {
    relay_rig rig;
    genesis(rig);
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    rig.start();

    auto const parent = make_tx(90);
    auto const child = make_spend(91, parent->hash(), 0);
    rig.submit(tx_received{1, child});

    // While the first block is read, the one that resolves the orphan is
    // connected, and rings.
    rig.reading = [&rig, &parent, done = false](size_t) mutable {
        if (done) {
            return;
        }
        done = true;
        extend(rig, 2, {make_spend(92, parent->hash(), 0)});
        rig.channels.tip_moved();
    };
    extend(rig, 1);
    ring(rig);
    CHECK(rig.reads == 2u);
    CHECK_FALSE(kept_until_parent(rig, parent, child->hash()));
}

TEST_CASE("tx relay: after a reorganization the new branch's blocks resolve orphans",
          "[tx_relay][owner][orphans][block]") {
    relay_rig rig;
    genesis(rig);
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    extend(rig, 1);
    rig.start();

    auto const parent = make_tx(100);
    auto const child = make_spend(101, parent->hash(), 0);
    rig.submit(tx_received{1, child});

    // The block the relay last saw is replaced, at its own height, by one
    // that spends what the orphan spends, and another follows.
    rig.blocks.resize(1);
    extend(rig, 2, {make_spend(102, parent->hash(), 0)});
    extend(rig, 3);
    ring(rig);
    CHECK_FALSE(kept_until_parent(rig, parent, child->hash()));
}

TEST_CASE("tx relay: a block that cannot be read is read at the next ring",
          "[tx_relay][owner][orphans][block]") {
    relay_rig rig;
    genesis(rig);
    rig.judge = [&rig](domain::message::transaction const& tx) { return by_pool(rig, tx); };
    rig.start();

    auto const parent = make_tx(110);
    auto const child = make_spend(111, parent->hash(), 0);
    rig.submit(tx_received{1, child});

    extend(rig, 1, {make_spend(112, parent->hash(), 0)});
    rig.unreadable.insert(1);
    ring(rig);
    CHECK(rig.reads == 1u);

    rig.unreadable.clear();
    extend(rig, 2);
    ring(rig);
    CHECK(rig.reads == 3u);
    CHECK_FALSE(kept_until_parent(rig, parent, child->hash()));
}

TEST_CASE("tx relay: closing the channel ends both tasks, also before they ever ran",
          "[tx_relay][owner][shutdown]") {
    SECTION("while running") {
        relay_rig rig;
        rig.start();
        REQUIRE_FALSE(rig.ended(rig.owner));
        REQUIRE_FALSE(rig.ended(rig.admitter));
        rig.channels.close();
        drain(rig.ioc);
        CHECK(rig.ended(rig.owner));
        CHECK(rig.ended(rig.admitter));
    }
    SECTION("before they start") {
        relay_rig rig;
        rig.channels.close();
        rig.start();
        CHECK(rig.ended(rig.owner));
        CHECK(rig.ended(rig.admitter));
    }
}

// -----------------------------------------------------------------------------
// The read loop's side
// -----------------------------------------------------------------------------

namespace {

network::raw_message wire(std::string const& command, data_chunk payload) {
    network::raw_message raw;
    raw.heading = domain::message::heading(0xdab5bffa, command, uint32_t(payload.size()), 0);
    raw.payload = std::move(payload);
    return raw;
}

// Serialized at the version the read loop parses it with.
template <typename Message>
data_chunk payload_of(Message const& message, network::peer_session::ptr const& peer) {
    auto const version = peer->negotiated_version();
    data_chunk out(message.serialized_size(version));
    byte_writer writer(out);
    REQUIRE(message.to_data(writer, version).has_value());
    return out;
}

std::vector<tx_relay_command> submitted(p2p_node& node) {
    std::vector<tx_relay_command> out;
    std::error_code ec;
    while (auto command = node.tx_relay().commands.try_receive(ec)) {
        out.push_back(std::move(*command));
    }
    return out;
}

} // namespace

TEST_CASE("relay routing: the transaction entries of an inv go to the relay with its blocks to theirs",
          "[tx_relay][routing]") {
    using domain::message::inventory_vector;
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9501);

    auto const block = txid_of(0xb1);
    auto const tx = txid_of(0x71);
    auto inv = domain::message::inventory::create(inventory_vector::list{
        {inventory_vector::type_id::block, block},
        {inventory_vector::type_id::transaction, tx}});
    REQUIRE(inv.has_value());
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::inventory::command, payload_of(*inv, peer))));

    auto const got = submitted(node);
    REQUIRE(got.size() == 1u);
    auto const* announced = std::get_if<tx_announced>(&got[0]);
    REQUIRE(announced != nullptr);
    CHECK(announced->peer == peer);
    CHECK(announced->txids == hash_list{tx});
    CHECK(node.take_announced_blocks().hashes == hash_list{block});
}

TEST_CASE("relay routing: a tx is consumed and handed over, a notfound handed over and carried on",
          "[tx_relay][routing]") {
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9502);

    auto const tx = make_tx(11);
    CHECK(node.observe_transaction_message(peer, wire(domain::message::transaction::command, payload_of(*tx, peer))));
    auto missing = domain::message::not_found::create(hash_list{txid_of(0x72)},
        domain::message::inventory_vector::type_id::transaction);
    REQUIRE(missing.has_value());
    CHECK_FALSE(node.observe_transaction_message(peer, wire(domain::message::not_found::command, payload_of(*missing, peer))));
    // Neither is anything else's business.
    CHECK_FALSE(node.observe_transaction_message(peer, wire(domain::message::ping::command, data_chunk(8, 0))));

    auto const got = submitted(node);
    REQUIRE(got.size() == 2u);
    auto const* received = std::get_if<tx_received>(&got[0]);
    REQUIRE(received != nullptr);
    CHECK(received->peer == 9502u);
    CHECK(received->tx->hash() == tx->hash());
    auto const* not_found = std::get_if<tx_not_found>(&got[1]);
    REQUIRE(not_found != nullptr);
    CHECK(not_found->txids == hash_list{txid_of(0x72)});
}

TEST_CASE("relay routing: a node that does not relay hands nothing over",
          "[tx_relay][routing]") {
    using domain::message::inventory_vector;
    ::asio::io_context ioc;
    auto settings = relay_settings();
    settings.relay_transactions = false;
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9503);

    auto inv = domain::message::inventory::create(inventory_vector::list{
        {inventory_vector::type_id::transaction, txid_of(0x73)}});
    REQUIRE(inv.has_value());
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::inventory::command, payload_of(*inv, peer))));
    CHECK(node.observe_transaction_message(peer, wire(domain::message::transaction::command, payload_of(*make_tx(12), peer))));
    CHECK(submitted(node).empty());
}

TEST_CASE("relay routing: a relay that cannot take more refuses at once",
          "[tx_relay][routing][saturation]") {
    // Nothing drains the channel here. The read loop must still never wait:
    // one past capacity is refused on the spot, and the message is consumed.
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9504);

    auto const capacity = tx_relay_channels::command_capacity;
    for (size_t i = 0; i < capacity; ++i) {
        REQUIRE(node.observe_transaction_message(peer,
            wire(domain::message::transaction::command, payload_of(*make_tx(1000 + uint32_t(i)), peer))));
    }
    CHECK(node.observe_transaction_message(peer,
        wire(domain::message::transaction::command, payload_of(*make_tx(5000), peer))));
    CHECK(submitted(node).size() == capacity);
}

TEST_CASE("relay routing: what the relay cannot take is kept by the read loop and handed over later",
          "[tx_relay][routing][saturation]") {
    using domain::message::inventory_vector;
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9505);

    // Full, with nothing taking from it.
    while (node.tx_relay().submit(tx_not_found{1, {}})) {
    }

    tx_relay_backlog backlog;
    auto const tx = txid_of(0x74);
    auto inv = domain::message::inventory::create(inventory_vector::list{
        {inventory_vector::type_id::transaction, tx}});
    REQUIRE(inv.has_value());
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::inventory::command, payload_of(*inv, peer)), backlog));
    CHECK(backlog.size() == 1u);

    // A turn of the read loop with the relay still full keeps it.
    backlog.flush(node.tx_relay());
    CHECK(backlog.size() == 1u);

    // With room, the next turn hands it over.
    auto const before = submitted(node);
    REQUIRE(before.size() == tx_relay_channels::command_capacity);
    backlog.flush(node.tx_relay());
    CHECK(backlog.empty());
    auto const after = submitted(node);
    REQUIRE(after.size() == 1u);
    auto const* announced = std::get_if<tx_announced>(&after[0]);
    REQUIRE(announced != nullptr);
    CHECK(announced->txids == hash_list{tx});
}

TEST_CASE("relay backlog: bounded per peer, in order", "[tx_relay][routing][saturation]") {
    tx_relay_channels channels;
    while (channels.submit(tx_not_found{1, {}})) {
    }
    tx_relay_backlog backlog;
    for (size_t i = 0; i < tx_relay_backlog::max_transactions; ++i) {
        REQUIRE(backlog.submit(channels, tx_received{1, make_tx(7000 + uint32_t(i))}));
    }
    CHECK_FALSE(backlog.submit(channels, tx_received{1, make_tx(8000)}));
    CHECK(backlog.submit(channels, tx_announced{nullptr, {txid_of(1)}}));

    std::error_code ec;
    while (channels.commands.try_receive(ec)) {
    }
    backlog.flush(channels);
    CHECK(backlog.empty());
    auto const first = channels.commands.try_receive(ec);
    REQUIRE(first);
    auto const* received = std::get_if<tx_received>(&*first);
    REQUIRE(received != nullptr);
    CHECK(received->tx->hash() == make_tx(7000)->hash());
}

// -----------------------------------------------------------------------------
// What the read loop records for announcing
// -----------------------------------------------------------------------------

TEST_CASE("relay routing: what a peer announces or sends is known to it, and not announced back",
          "[tx_relay][routing][tx_announcement]") {
    using domain::message::inventory_vector;
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9510);

    auto const announced = txid_of(0x81);
    auto inv = domain::message::inventory::create(inventory_vector::list{
        {inventory_vector::type_id::transaction, announced}});
    REQUIRE(inv.has_value());
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::inventory::command, payload_of(*inv, peer))));
    auto const sent = make_tx(81);
    CHECK(node.observe_transaction_message(peer, wire(domain::message::transaction::command, payload_of(*sent, peer))));

    CHECK(peer->tx_inventory().known(announced));
    CHECK(peer->tx_inventory().known(sent->hash()));
    CHECK_FALSE(peer->tx_inventory().known(txid_of(0x82)));
}

TEST_CASE("relay routing: a feefilter is consumed and kept, unless it is not a valid amount",
          "[tx_relay][routing][tx_announcement]") {
    ::asio::io_context ioc;
    auto const settings = relay_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9511);
    CHECK(peer->tx_inventory().fee_filter() == 0u);

    domain::message::fee_filter const filter{3000};
    CHECK(node.observe_transaction_message(peer, wire(domain::message::fee_filter::command, payload_of(filter, peer))));
    CHECK(peer->tx_inventory().fee_filter() == 3000u);

    domain::message::fee_filter const invalid{max_money() + 1};
    CHECK(node.observe_transaction_message(peer, wire(domain::message::fee_filter::command, payload_of(invalid, peer))));
    CHECK(peer->tx_inventory().fee_filter() == 3000u);

    // Zero filters nothing again.
    domain::message::fee_filter const none{0};
    CHECK(node.observe_transaction_message(peer, wire(domain::message::fee_filter::command, payload_of(none, peer))));
    CHECK(peer->tx_inventory().fee_filter() == 0u);
}
