// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/ping.hpp>

#include <kth/node/detail/header_ingest.hpp>
#include <kth/node/detail/header_walk_tasks.hpp>

#include "header_peer_harness.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::detail;
using namespace kth::test;

// =============================================================================
// The header ingest, and the coordinator's operations around the walk (#710)
// =============================================================================
//
// The ingest is where every header arrives, asked for or not. Its queues have
// one owner task; read loops reach it only with a non-waiting submission; a
// refusal is local pressure, rung on a one-slot debt doorbell and never scored.
// The coordinator's operations — the stimulus being sent, its deadline, the
// retry of a parked walk — are tasks it alone starts, replaces and cancels,
// each through its own strand.
//
// Every control here runs an io_context by hand: `drain` runs what is ready and
// returns when nothing more can happen without time passing.

namespace {

domain::message::header::list batch(uint32_t tag, size_t count) {
    domain::message::header::list out;
    for (size_t i = 0; i < count; ++i) {
        out.emplace_back(1u, null_hash, null_hash, tag + uint32_t(i), 0x1d00ffffu, tag);
    }
    return out;
}

network::settings peer_config() {
    network::settings settings;
    settings.hosts_file = "/tmp/kth_ingest_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_ingest_banlist_nonexistent.dat";
    return settings;
}

template <typename Event>
std::optional<Event> next_event(sync::sync_coordinator_event_channel& events) {
    std::optional<Event> found;
    while (true) {
        std::optional<sync::sync_coordinator_event> event;
        bool const got = events.try_receive([&](std::error_code, sync::sync_coordinator_event value) {
            event = std::move(value);
        });
        if ( ! got || ! event) break;
        if (auto* typed = std::get_if<Event>(&*event)) {
            found = *typed;
            break;
        }
    }
    return found;
}

bool debt_rung(header_ingest_channels& channels) {
    return channels.debt.try_receive([](std::error_code, headers_owed) {});
}

// A task group that starts what it is handed at once.
struct started_tasks {
    ::asio::io_context& ioc;
    std::vector<std::future<void>> futures;

    spawner spawn() {
        return [this](::asio::awaitable<void> task) {
            futures.push_back(::asio::co_spawn(ioc, std::move(task), ::asio::use_future));
        };
    }

    bool all_ended() const {
        return std::all_of(futures.begin(), futures.end(), [](auto const& f) {
            return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
    }
};

// A task group that has not got round to starting anything: it keeps what it
// is handed, and starts each only when the control says so.
struct held_tasks {
    std::vector<::asio::awaitable<void>> held;

    spawner spawn() {
        return [this](::asio::awaitable<void> task) {
            held.push_back(std::move(task));
        };
    }
};

// A session on one end of a loopback connection whose write loop is not
// running: its outbound queue fills and nothing drains it until run() starts.
struct backed_up_session {
    ::asio::ip::tcp::socket client;
    network::peer_session::ptr session;
};

backed_up_session backed_up(::asio::io_context& ioc, network::settings const& config, uint64_t nonce) {
    ::asio::ip::tcp::acceptor acceptor(ioc, {::asio::ip::address_v4::loopback(), 0});
    ::asio::ip::tcp::socket client(ioc);
    ::asio::ip::tcp::socket server(ioc);
    client.connect(acceptor.local_endpoint());
    acceptor.accept(server);
    auto session = std::make_shared<network::peer_session>(std::move(server), config, false);
    session->set_nonce(nonce);
    return {std::move(client), std::move(session)};
}

// Fill a session's outbound queue to capacity, with pings.
void fill(::asio::io_context& ioc, network::peer_session::ptr const& session) {
    auto filled = ::asio::co_spawn(ioc, [session]() -> ::asio::awaitable<void> {
        for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
            auto const ec = co_await session->send(domain::message::ping{uint64_t(i)});
            REQUIRE(ec == error::success);
        }
    }, ::asio::use_future);
    drain(ioc);
    REQUIRE(filled.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    filled.get();
}

// What a session wrote, once its write loop runs: every message's command, and
// for a getheaders the first hash of its locator.
struct written {
    std::string command;
    std::optional<hash_digest> locator;
};

std::vector<written> run_and_read(::asio::io_context& ioc, network::peer_session::ptr const& session,
                                  ::asio::ip::tcp::socket& client, size_t expected) {
    using namespace std::chrono_literals;
    ::asio::co_spawn(ioc, [session]() -> ::asio::awaitable<void> {
        (void)co_await session->run();
    }, ::asio::detached);

    data_chunk bytes;
    std::vector<written> out;
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (out.size() < expected && std::chrono::steady_clock::now() < deadline) {
        ioc.restart();
        ioc.run_for(10ms);
        while (client.available() > 0) {
            data_chunk more(client.available());
            ::asio::read(client, ::asio::buffer(more));
            bytes.insert(bytes.end(), more.begin(), more.end());
        }
        out.clear();
        size_t at = 0;
        while (at + 24 <= bytes.size()) {
            char command[13] = {};
            std::memcpy(command, bytes.data() + at + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, bytes.data() + at + 16, 4);
            if (at + 24 + length > bytes.size()) break;
            written message{command, std::nullopt};
            if (message.command == domain::message::get_headers::command) {
                data_chunk payload(bytes.begin() + long(at + 24), bytes.begin() + long(at + 24 + length));
                byte_reader reader(payload);
                auto parsed = domain::message::get_headers::from_data(reader, session->negotiated_version());
                REQUIRE(parsed.has_value());
                message.locator = parsed->start_hashes().front();
            }
            out.push_back(std::move(message));
            at += 24 + length;
        }
    }
    return out;
}

size_t count_command(std::vector<written> const& sent, std::string const& command) {
    return size_t(std::count_if(sent.begin(), sent.end(),
        [&](auto const& m) { return m.command == command; }));
}

hash_digest hash_of(uint8_t tag) {
    hash_digest out = null_hash;
    out[0] = tag;
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
// The queues, as state
// -----------------------------------------------------------------------------

TEST_CASE("header ingest: a full peer queue refuses and leaves the others alone",
          "[header_ingest]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    auto b = make_peer(ioc, config, 2);
    header_ingest queues;

    for (size_t i = 0; i < header_ingest::per_peer_capacity; ++i) {
        CHECK(queues.offer(a, batch(100 + uint32_t(i), 1)));
    }
    CHECK_FALSE(queues.offer(a, batch(200, 1)));
    CHECK(queues.queued(1) == header_ingest::per_peer_capacity);
    CHECK(queues.offer(b, batch(300, 1)));
    CHECK(queues.queued(2) == 1u);
}

TEST_CASE("header ingest: the queues are bounded in total, whoever sends",
          "[header_ingest][saturation][churn]") {
    // One batch each from more peers than the total admits: each is within its
    // own share, and the total still refuses. A refused peer leaves nothing
    // behind, and room made by a take is room for anyone.
    ::asio::io_context ioc;
    auto const config = peer_config();
    header_ingest queues;
    std::vector<network::peer_session::ptr> peers;
    for (size_t i = 0; i <= header_ingest::total_capacity; ++i) {
        peers.push_back(make_peer(ioc, config, 100 + i));
    }

    for (size_t i = 0; i < header_ingest::total_capacity; ++i) {
        REQUIRE(queues.offer(peers[i], batch(uint32_t(i), 1)));
    }
    CHECK(queues.total() == header_ingest::total_capacity);

    auto const& late = peers.back();
    CHECK_FALSE(queues.offer(late, batch(9999, 1)));
    CHECK(queues.queued(late->nonce()) == 0u);
    CHECK(queues.total() == header_ingest::total_capacity);

    REQUIRE(queues.take().has_value());
    CHECK(queues.offer(late, batch(9999, 1)));
    CHECK(queues.total() == header_ingest::total_capacity);
}

TEST_CASE("header ingest: one batch per peer per turn", "[header_ingest]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    auto b = make_peer(ioc, config, 2);
    auto c = make_peer(ioc, config, 3);
    header_ingest queues;

    REQUIRE(queues.offer(a, batch(10, 1)));
    REQUIRE(queues.offer(a, batch(11, 1)));
    REQUIRE(queues.offer(b, batch(20, 1)));
    REQUIRE(queues.offer(c, batch(30, 1)));

    std::vector<uint64_t> order;
    std::vector<uint32_t> stamps;
    while (auto taken = queues.take()) {
        order.push_back(taken->peer->nonce());
        stamps.push_back(taken->headers[0].timestamp());
    }
    // A's second batch waits behind B and C, not in front of them; and A's own
    // batches come out in the order A sent them.
    CHECK(order == std::vector<uint64_t>{1, 2, 3, 1});
    CHECK(stamps == std::vector<uint32_t>{10, 20, 30, 11});
    CHECK(queues.empty());
    CHECK(queues.queued(1) == 0u);
}

// -----------------------------------------------------------------------------
// The owner, and the channels into and out of it
// -----------------------------------------------------------------------------

TEST_CASE("header ingest task: a pull is answered at once, or by the next arrival",
          "[header_ingest][tasks]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    header_ingest_channels channels(ioc.get_executor());
    ingest_handoff_channel handoff(ioc, 1);

    auto owner = ::asio::co_spawn(ioc, header_ingest_task(channels, handoff), ::asio::use_future);

    // Pull first: nothing to hand over yet.
    REQUIRE(channels.commands.try_send(std::error_code{}, ingest_pull{}));
    drain(ioc);
    CHECK_FALSE(handoff.try_receive([](std::error_code, ingested_headers) {}));

    // The next arrival answers it.
    REQUIRE(channels.submit(a, batch(7, 3)));
    drain(ioc);
    std::optional<ingested_headers> got;
    REQUIRE(handoff.try_receive([&](std::error_code, ingested_headers value) { got = std::move(value); }));
    REQUIRE(got.has_value());
    CHECK(got->peer == a);
    CHECK(got->headers.size() == 3u);

    // Queued first, pulled after: answered at once.
    REQUIRE(channels.submit(a, batch(8, 1)));
    drain(ioc);
    REQUIRE(channels.commands.try_send(std::error_code{}, ingest_pull{}));
    drain(ioc);
    CHECK(handoff.try_receive([](std::error_code, ingested_headers) {}));
    CHECK_FALSE(debt_rung(channels));

    channels.close();
    drain(ioc);
    REQUIRE(owner.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK_FALSE(handoff.is_open());
}

TEST_CASE("header ingest task: a peer over its share is refused, owed, and fair to the rest",
          "[header_ingest][tasks][saturation]") {
    // A floods; B sends one batch. A's excess is refused by the owner — rung as
    // a debt, scored against nobody — and B's batch does not wait behind A's.
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    auto b = make_peer(ioc, config, 2);
    header_ingest_channels channels(ioc.get_executor());
    ingest_handoff_channel handoff(ioc, 1);
    auto owner = ::asio::co_spawn(ioc, header_ingest_task(channels, handoff), ::asio::use_future);

    for (uint32_t i = 0; i < 10; ++i) {
        // The read loop's side never refuses here: the owner takes every
        // submission at once, and does the refusing itself.
        CHECK(channels.submit(a, batch(100 + i, 1)));
    }
    CHECK(channels.submit(b, batch(500, 1)));
    drain(ioc);

    CHECK(debt_rung(channels));             // A's excess is owed
    CHECK_FALSE(debt_rung(channels));       // once: coalesced
    CHECK(a->misbehavior_score() == 0);

    std::vector<uint64_t> order;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(channels.commands.try_send(std::error_code{}, ingest_pull{}));
        drain(ioc);
        std::optional<ingested_headers> got;
        REQUIRE(handoff.try_receive([&](std::error_code, ingested_headers value) { got = std::move(value); }));
        order.push_back(got->peer->nonce());
    }
    CHECK(order == std::vector<uint64_t>{1, 2, 1});

    channels.close();
    drain(ioc);
    REQUIRE(owner.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
}

TEST_CASE("header ingest: a submission the channel cannot take is owed, never waited for",
          "[header_ingest][saturation]") {
    // No owner running: the command channel fills. The read loop's submission
    // returns at once, refused — and the refusal is on the debt doorbell.
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    header_ingest_channels channels(ioc.get_executor());

    for (size_t i = 0; i < header_ingest_channels::command_capacity; ++i) {
        REQUIRE(channels.submit(a, batch(uint32_t(i), 1)));
    }
    CHECK_FALSE(debt_rung(channels));

    CHECK_FALSE(channels.submit(a, batch(9999, 1)));
    CHECK(debt_rung(channels));

    // Two refusals with the doorbell already rung leave one ring: the pending
    // ring is collected after both, which is all either needs.
    CHECK_FALSE(channels.submit(a, batch(9998, 1)));
    CHECK_FALSE(channels.submit(a, batch(9997, 1)));
    CHECK(debt_rung(channels));
    CHECK_FALSE(debt_rung(channels));
}

TEST_CASE("header debt bridge: every ring reaches the coordinator, after the refusal",
          "[header_ingest][tasks][saturation]") {
    ::asio::io_context ioc;
    header_ingest_channels channels(ioc.get_executor());
    sync::sync_coordinator_event_channel events(ioc, 100);
    auto bridge = ::asio::co_spawn(ioc, header_debt_bridge(channels, events), ::asio::use_future);

    channels.owe();
    drain(ioc);
    CHECK(next_event<sync::headers_owed_event>(events).has_value());

    // A later refusal rings again, and is carried again: no debt is left behind
    // a ring that was already taken.
    channels.owe();
    drain(ioc);
    CHECK(next_event<sync::headers_owed_event>(events).has_value());
    CHECK_FALSE(next_event<sync::headers_owed_event>(events).has_value());

    channels.close();
    drain(ioc);
    REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
}

TEST_CASE("header ingest: the owner keeps taking while validation is full",
          "[header_ingest][tasks][saturation]") {
    // Validation has no room; the forwarder waits for it. The owner does not:
    // submissions keep being taken — queued up to each peer's share, owed
    // beyond it — so the read loops never meet a full command channel.
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    header_ingest_channels channels(ioc.get_executor());
    ingest_handoff_channel handoff(ioc, 1);
    sync::header_validation_input_channel validation(ioc, 1);
    REQUIRE(validation.try_send(std::error_code{}, sync::stop_request{}));      // full

    auto owner = ::asio::co_spawn(ioc, header_ingest_task(channels, handoff), ::asio::use_future);
    auto forwarder = ::asio::co_spawn(ioc,
        header_ingest_forwarder(channels, handoff, validation), ::asio::use_future);

    for (size_t i = 0; i < 2 * header_ingest_channels::command_capacity; ++i) {
        CHECK(channels.submit(a, batch(uint32_t(i), 1)));
        drain(ioc);
    }
    CHECK(debt_rung(channels));

    // Room in validation: the batch the forwarder held goes in.
    std::optional<sync::header_validation_input> first;
    REQUIRE(validation.try_receive([&](std::error_code, sync::header_validation_input v) { first = std::move(v); }));
    drain(ioc);
    std::optional<sync::header_validation_input> second;
    REQUIRE(validation.try_receive([&](std::error_code, sync::header_validation_input v) { second = std::move(v); }));
    CHECK(std::holds_alternative<sync::downloaded_headers>(*second));

    // Shutdown with the forwarder waiting on validation and the owner on its
    // channel: both end.
    channels.close();
    validation.cancel();
    validation.close();
    drain(ioc);
    CHECK(owner.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(forwarder.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
}

TEST_CASE("header ingest: churn with validation blocked retains a bounded amount, owed beyond it",
          "[header_ingest][tasks][saturation][churn]") {
    // Validation is blocked, so nothing leaves the ingest. Sessions come, each
    // submits its share, and goes — the test keeps no reference, only a weak
    // one. The owner drains its command channel between waves, so the channel
    // never pushes back. What bounds what the ingest keeps is then the ingest
    // itself: twice the churn must not keep twice the sessions alive. Beyond
    // the bound, refusals are owed, never waited for; when validation drains,
    // what was kept goes through; and shutdown lets every session go.
    ::asio::io_context ioc;
    auto const config = peer_config();
    header_ingest_channels channels(ioc.get_executor());
    ingest_handoff_channel handoff(ioc, 1);
    sync::header_validation_input_channel validation(ioc, 1);
    REQUIRE(validation.try_send(std::error_code{}, sync::stop_request{}));      // blocked

    auto owner = ::asio::co_spawn(ioc, header_ingest_task(channels, handoff), ::asio::use_future);
    auto forwarder = ::asio::co_spawn(ioc,
        header_ingest_forwarder(channels, handoff, validation), ::asio::use_future);

    std::vector<std::weak_ptr<network::peer_session>> departed;
    bool every_submission_returned = true;
    uint64_t nonce = 1000;

    auto wave = [&](size_t sessions) {
        for (size_t i = 0; i < sessions; ++i) {
            auto peer = make_peer(ioc, config, nonce++);
            // Each within its own per-peer share.
            every_submission_returned &= channels.submit(peer, batch(uint32_t(nonce), 1));
            every_submission_returned &= channels.submit(peer, batch(uint32_t(nonce) + 1, 1));
            departed.push_back(peer);
            drain(ioc);                 // the owner takes them; the channel is empty again
        }
    };
    auto alive = [&] {
        return size_t(std::count_if(departed.begin(), departed.end(),
            [](auto const& w) { return ! w.expired(); }));
    };

    wave(200);
    auto const kept_after_one = alive();
    CHECK(debt_rung(channels));             // beyond the bound: owed

    wave(200);
    auto const kept_after_two = alive();

    // Every submission returned at once: the read loops never waited.
    CHECK(every_submission_returned);
    // Twice the churn, not more kept: the ingest is bounded, not the churn.
    CHECK(kept_after_two == kept_after_one);
    CHECK(kept_after_one < 200u);
    CHECK(debt_rung(channels));

    // Validation drains: what was kept goes through, and the ingest empties.
    size_t delivered = 0;
    for (size_t i = 0; i < 10000; ++i) {
        std::optional<sync::header_validation_input> item;
        if ( ! validation.try_receive([&](std::error_code, sync::header_validation_input v) { item = std::move(v); })) {
            drain(ioc);
            if ( ! validation.try_receive([&](std::error_code, sync::header_validation_input v) { item = std::move(v); })) {
                break;
            }
        }
        if (std::holds_alternative<sync::downloaded_headers>(*item)) ++delivered;
        drain(ioc);
    }
    CHECK(delivered > 0u);
    CHECK(alive() <= 1u);                   // at most the batch the forwarder holds

    // Shutdown lets every session go.
    channels.close();
    validation.cancel();
    validation.close();
    drain(ioc);
    REQUIRE(owner.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    REQUIRE(forwarder.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(alive() == 0u);
}

// -----------------------------------------------------------------------------
// The stimulus: sent by its own task, withdrawn if the walk moves past it
// -----------------------------------------------------------------------------

TEST_CASE("stimulus sender: taken by the queue is reported as accepted",
          "[header_ingest][tasks][stimulus]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto live = make_peer(ioc, config, 1);
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());

    sender.send(live, null_hash, 7);
    drain(ioc);
    auto const accepted = next_event<sync::header_stimulus_accepted>(events);
    REQUIRE(accepted.has_value());
    CHECK(accepted->seq == 7u);
    CHECK(group.all_ended());
}

TEST_CASE("stimulus sender: a stopped session is reported as failed",
          "[header_ingest][tasks][stimulus]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto stopped = make_peer(ioc, config, 2);
    stopped->stop();
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());

    sender.send(stopped, null_hash, 8);
    drain(ioc);
    auto const failed = next_event<sync::header_stimulus_failed>(events);
    REQUIRE(failed.has_value());
    CHECK(failed->seq == 8u);
    CHECK_FALSE(next_event<sync::header_stimulus_accepted>(events).has_value());
}

TEST_CASE("stimulus sender: withdrawn while it waits for room, it is never sent",
          "[header_ingest][tasks][stimulus]") {
    // The peer's queue is full. The stimulus waits for room — that is what a
    // send does — and the walk moves past it. When room returns, what goes out
    // is what the queue had taken: no getheaders.
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto [client, session] = backed_up(ioc, config, 3);
    fill(ioc, session);
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());

    sender.send(session, hash_of(1), 9);
    drain(ioc);
    REQUIRE_FALSE(group.all_ended());               // waiting for room

    sender.cancel();
    drain(ioc);
    CHECK(group.all_ended());                       // withdrawn: nothing left waiting

    auto const sent = run_and_read(ioc, session, client, network::peer_session::outbound_capacity);
    CHECK(count_command(sent, domain::message::ping::command) == network::peer_session::outbound_capacity);
    CHECK(count_command(sent, domain::message::get_headers::command) == 0u);
    CHECK_FALSE(next_event<sync::header_stimulus_accepted>(events).has_value());
    CHECK_FALSE(next_event<sync::header_stimulus_failed>(events).has_value());
    session->stop();
    ioc.restart();
    ioc.run_for(std::chrono::milliseconds(100));
}

TEST_CASE("stimulus sender: withdrawn before its task starts, it does not send",
          "[header_ingest][tasks][stimulus][race]") {
    // The window: send() has handed the task over, nothing has started it, and
    // the walk withdraws it. The withdrawal reaches the strand first; the task
    // then starts and must not send.
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto [client, session] = backed_up(ioc, config, 4);
    sync::sync_coordinator_event_channel events(ioc, 100);
    held_tasks group;
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());

    sender.send(session, hash_of(2), 10);
    REQUIRE(group.held.size() == 1u);
    sender.cancel();
    drain(ioc);                                     // the withdrawal, and nothing else

    auto task = ::asio::co_spawn(ioc, std::move(group.held[0]), ::asio::use_future);
    drain(ioc);
    CHECK(task.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK_FALSE(next_event<sync::header_stimulus_accepted>(events).has_value());

    // And nothing reached the queue: a ping sent now is the first thing out.
    auto ping = ::asio::co_spawn(ioc, [s = session]() -> ::asio::awaitable<code> {
        co_return co_await s->send(domain::message::ping{uint64_t(77)});
    }, ::asio::use_future);
    drain(ioc);
    REQUIRE(ping.get() == error::success);
    auto const sent = run_and_read(ioc, session, client, 1);
    REQUIRE_FALSE(sent.empty());
    CHECK(sent.front().command == domain::message::ping::command);
    CHECK(count_command(sent, domain::message::get_headers::command) == 0u);
    session->stop();
    ioc.restart();
    ioc.run_for(std::chrono::milliseconds(100));
}

TEST_CASE("stimulus sender: a replaced stimulus is withdrawn, and room completes the new one",
          "[header_ingest][tasks][stimulus]") {
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto [client, session] = backed_up(ioc, config, 5);
    fill(ioc, session);
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());

    sender.send(session, hash_of(5), 11);           // waits for room
    drain(ioc);
    sender.send(session, hash_of(6), 12);           // replaces it, also waits
    drain(ioc);
    REQUIRE(group.futures.size() == 2u);
    CHECK(group.futures[0].wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(group.futures[1].wait_for(std::chrono::seconds(0)) == std::future_status::timeout);

    // Room returning is what completes the current one.
    auto const sent = run_and_read(ioc, session, client, network::peer_session::outbound_capacity + 1);
    CHECK(count_command(sent, domain::message::get_headers::command) == 1u);
    for (auto const& m : sent) {
        if (m.locator) CHECK(*m.locator == hash_of(6));
    }
    auto const accepted = next_event<sync::header_stimulus_accepted>(events);
    REQUIRE(accepted.has_value());
    CHECK(accepted->seq == 12u);
    CHECK_FALSE(next_event<sync::header_stimulus_accepted>(events).has_value());
    session->stop();
    ioc.restart();
    ioc.run_for(std::chrono::milliseconds(100));
}

// -----------------------------------------------------------------------------
// The walk's scheduled events
// -----------------------------------------------------------------------------

TEST_CASE("walk timer: delivers once, after its delay, and only the last arming",
          "[header_ingest][tasks][timer]") {
    using namespace std::chrono_literals;
    ::asio::io_context ioc;
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    walk_timer timer(ioc.get_executor(), events, group.spawn());

    using clock = std::chrono::steady_clock;
    auto receive = [&]() -> ::asio::awaitable<
        std::pair<sync::sync_coordinator_event, clock::time_point>> {
        auto event = co_await events.async_receive(::asio::use_awaitable);
        co_return std::pair{std::move(event), clock::now()};
    };
    auto received = ::asio::co_spawn(ioc, receive(), ::asio::use_future);

    timer.arm(30ms, sync::header_walk_timeout{.seq = 5});
    auto const armed_at = clock::now();
    timer.arm(30ms, sync::header_walk_timeout{.seq = 6});      // replaces 5

    // A loaded runner may resume after the deadline even during run_for(10ms).
    // Observe delivery once and check its lower bound, without consuming the
    // event in an early probe or imposing an upper bound on scheduler latency.
    ioc.run();
    auto const [event, delivered_at] = received.get();
    auto const* fired = std::get_if<sync::header_walk_timeout>(&event);
    REQUIRE(fired != nullptr);
    CHECK(fired->seq == 6u);
    CHECK(delivered_at - armed_at >= 30ms);
    CHECK_FALSE(next_event<sync::header_walk_timeout>(events).has_value());
    CHECK(group.futures.size() == 2u);
    CHECK(group.all_ended());
}

TEST_CASE("walk timer: cancelled while it waits, it delivers nothing and ends at once",
          "[header_ingest][tasks][timer][shutdown]") {
    using namespace std::chrono_literals;
    ::asio::io_context ioc;
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    walk_timer timer(ioc.get_executor(), events, group.spawn());

    timer.arm(10s, sync::header_walk_retry{.generation = 1});
    drain(ioc);
    REQUIRE_FALSE(group.all_ended());

    timer.cancel();
    drain(ioc);
    CHECK(group.all_ended());
    CHECK_FALSE(next_event<sync::header_walk_retry>(events).has_value());
}

TEST_CASE("walk timer: cancelled before its task starts, the task does not wait",
          "[header_ingest][tasks][timer][race]") {
    using namespace std::chrono_literals;
    ::asio::io_context ioc;
    sync::sync_coordinator_event_channel events(ioc, 100);
    held_tasks group;
    walk_timer timer(ioc.get_executor(), events, group.spawn());

    timer.arm(60s, sync::header_walk_retry{.generation = 1});
    REQUIRE(group.held.size() == 1u);
    timer.cancel();
    drain(ioc);                                     // the cancellation, and nothing else

    auto task = ::asio::co_spawn(ioc, std::move(group.held[0]), ::asio::use_future);
    drain(ioc);                                     // no time passes
    CHECK(task.wait_for(0s) == std::future_status::ready);
    CHECK_FALSE(next_event<sync::header_walk_retry>(events).has_value());
}

TEST_CASE("walk timer: replaced before its task starts, only the replacement waits",
          "[header_ingest][tasks][timer][race]") {
    using namespace std::chrono_literals;
    ::asio::io_context ioc;
    sync::sync_coordinator_event_channel events(ioc, 100);
    held_tasks group;
    walk_timer timer(ioc.get_executor(), events, group.spawn());

    timer.arm(60s, sync::header_walk_retry{.generation = 1});
    timer.arm(30ms, sync::header_walk_retry{.generation = 2});
    REQUIRE(group.held.size() == 2u);
    drain(ioc);

    auto old_task = ::asio::co_spawn(ioc, std::move(group.held[0]), ::asio::use_future);
    auto new_task = ::asio::co_spawn(ioc, std::move(group.held[1]), ::asio::use_future);
    drain(ioc);
    CHECK(old_task.wait_for(0s) == std::future_status::ready);
    CHECK(new_task.wait_for(0s) == std::future_status::timeout);

    ioc.restart();
    ioc.run_for(500ms);
    CHECK(new_task.wait_for(0s) == std::future_status::ready);
    auto const fired = next_event<sync::header_walk_retry>(events);
    REQUIRE(fired.has_value());
    CHECK(fired->generation == 2u);
    CHECK_FALSE(next_event<sync::header_walk_retry>(events).has_value());
}

TEST_CASE("walk retry delay: doubles from the base, never past the cap",
          "[header_ingest][tasks][timer]") {
    using namespace std::chrono_literals;
    CHECK(walk_retry_delay(100ms, 1000ms, 0) == 100ms);
    CHECK(walk_retry_delay(100ms, 1000ms, 1) == 200ms);
    CHECK(walk_retry_delay(100ms, 1000ms, 3) == 800ms);
    CHECK(walk_retry_delay(100ms, 1000ms, 4) == 1000ms);
    CHECK(walk_retry_delay(100ms, 1000ms, 31) == 1000ms);
    CHECK(walk_retry_delay(5000ms, 1000ms, 0) == 1000ms);
    CHECK(walk_retry_delay(10s, 600s, 0) == 10s);
    CHECK(walk_retry_delay(10s, 600s, 6) == 600s);
}

TEST_CASE("header tasks: shutdown with operations armed and batches waiting ends at once",
          "[header_ingest][tasks][shutdown]") {
    // A stimulus waiting for room, a deadline and a retry minutes away, and the
    // ingest holding batches validation has no room for. Cancelling the
    // operations and closing the channels — what the coordinator and the node
    // do — ends every task, now.
    using namespace std::chrono_literals;
    ::asio::io_context ioc;
    auto const config = peer_config();
    auto a = make_peer(ioc, config, 1);
    auto [client, session] = backed_up(ioc, config, 6);
    fill(ioc, session);

    header_ingest_channels channels(ioc.get_executor());
    ingest_handoff_channel handoff(ioc, 1);
    sync::header_validation_input_channel validation(ioc, 1);
    sync::sync_coordinator_event_channel events(ioc, 100);
    started_tasks group{ioc, {}};
    stimulus_sender sender(ioc.get_executor(), events, group.spawn());
    walk_timer deadline(ioc.get_executor(), events, group.spawn());
    walk_timer retry(ioc.get_executor(), events, group.spawn());

    auto owner = ::asio::co_spawn(ioc, header_ingest_task(channels, handoff), ::asio::use_future);
    auto forwarder = ::asio::co_spawn(ioc,
        header_ingest_forwarder(channels, handoff, validation), ::asio::use_future);

    sender.send(session, hash_of(9), 1);
    deadline.arm(10min, sync::header_walk_timeout{.seq = 1});
    retry.arm(10min, sync::header_walk_retry{.generation = 3});
    REQUIRE(channels.submit(a, batch(1, 1)));
    REQUIRE(channels.submit(a, batch(2, 1)));
    REQUIRE(channels.submit(a, batch(3, 1)));
    drain(ioc);
    REQUIRE_FALSE(group.all_ended());
    REQUIRE(owner.wait_for(0s) == std::future_status::timeout);
    REQUIRE(forwarder.wait_for(0s) == std::future_status::timeout);

    auto const started = std::chrono::steady_clock::now();
    sender.cancel();
    deadline.cancel();
    retry.cancel();
    channels.close();
    validation.cancel();
    validation.close();
    drain(ioc);

    CHECK(group.all_ended());
    CHECK(owner.wait_for(0s) == std::future_status::ready);
    CHECK(forwarder.wait_for(0s) == std::future_status::ready);
    CHECK(std::chrono::steady_clock::now() - started < 1s);
    CHECK_FALSE(next_event<sync::header_walk_timeout>(events).has_value());
    CHECK_FALSE(next_event<sync::header_walk_retry>(events).has_value());
    CHECK_FALSE(next_event<sync::header_stimulus_accepted>(events).has_value());
    session->stop();
    ioc.restart();
    ioc.run_for(100ms);
}
