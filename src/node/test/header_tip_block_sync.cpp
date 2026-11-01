// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/orchestrator.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// The header walk through the real orchestrator (#705, #706, #710)
// =============================================================================
//
// The real sync_orchestrator, the real coordinator, the real organizer over a
// real chain, and peers that are real sessions over loopback sockets. What the
// node sends them is read off the socket — so a control that says "a getheaders
// went out, from this tip" is reading the bytes the node put on the wire. What
// they send back enters through p2p_node::observe_block_message, the entry the
// read loop hands every message to.
//
// Nothing in here correlates a `headers` with a `getheaders`, because nothing in
// the node does: each answer is just a message from a peer, and the walk makes
// of it what the organizer says about it.

namespace {

using peer_ptr = network::peer_session::ptr;

// Swap the default logger for one that keeps what was written.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        // Large enough that nothing these cases count can be evicted before
        // they count it: the coordinator's stop bridge alone writes several
        // lines every 100ms.
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(262144))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    void dump(std::string const& name) const {
        auto const path = std::filesystem::temp_directory_path() / ("kth_" + name + "_dump.log");
        std::ofstream out(path, std::ios::trunc);
        for (auto const& line : sink->last_formatted()) {
            out << line;
        }
        UNSCOPED_INFO("orchestrator log written to " << path.string());
    }

    [[nodiscard]] size_t first_index(std::string_view needle) const {
        auto const lines = sink->last_formatted();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(needle) != std::string::npos) return i;
        }
        return std::string::npos;
    }

    [[nodiscard]] size_t count(std::string_view needle) const {
        size_t n = 0;
        for (auto const& line : sink->last_formatted()) {
            if (line.find(needle) != std::string::npos) ++n;
        }
        return n;
    }
};

network::settings node_settings(std::string const& tag) {
    network::settings settings;
    settings.identifier = 0xdab5bffa;           // regtest
    settings.protocol_maximum = 70015;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;          // nothing dials out
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_" + tag + "_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_" + tag + "_banlist_nonexistent.dat";
    // Each case its own, and fresh: the node saves the peer database when it
    // stops, and the default shared file would carry one case's scores into
    // the next run's.
    auto const peers = std::filesystem::temp_directory_path() / ("kth_" + tag + "_peers.dat");
    std::filesystem::remove(peers);
    settings.peers_file = peers;
    return settings;
}

// Wait for a condition, bounded, returning the moment it is true.
bool wait_until(std::function<bool()> const& done, std::chrono::milliseconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// A peer session on one end of a loopback connection, run on a thread of its
// own; the test holds the other end and reads what the node sends.
struct wire_peer {
    ::asio::io_context ioc;
    ::asio::io_context client_ioc;          // owns the test's end; never run
    ::asio::ip::tcp::socket client{client_ioc};
    peer_ptr session;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard{::asio::make_work_guard(ioc)};
    std::thread runner;

    // Running from the start: what the node sends is written to the socket as
    // soon as it is queued.
    wire_peer(network::settings const& config, uint64_t nonce, uint32_t claimed_height)
        : wire_peer(config, nonce, claimed_height, false) {
        start();
    }

    // The session not running until start(): its outbound queue fills and
    // nothing drains it, as for a peer whose socket is backed up. Its context
    // runs from the start all the same, as every session's does on a node —
    // a stop() has work to complete on it.
    wire_peer(network::settings const& config, uint64_t nonce, uint32_t claimed_height, bool /*deferred*/) {
        ::asio::ip::tcp::acceptor acceptor(ioc, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(ioc);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);

        session = std::make_shared<network::peer_session>(std::move(server), config, false);
        session->set_nonce(nonce);
        domain::message::version version;
        version.set_start_height(claimed_height);
        session->set_peer_version(std::make_shared<domain::message::version const>(std::move(version)));
        runner = std::thread([this] { ioc.run(); });
    }

    void start() {
        ::asio::co_spawn(ioc, [peer = session]() -> ::asio::awaitable<void> {
            (void)co_await peer->run();
        }, ::asio::detached);
    }

    // Fill the outbound queue to capacity with pings, on the session's own
    // context. Answers how many it holds.
    size_t fill_outbound() {
        auto filled = ::asio::co_spawn(ioc, [peer = session]() -> ::asio::awaitable<size_t> {
            for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
                auto const ec = co_await peer->send(domain::message::ping{uint64_t(i)});
                if (ec != error::success) co_return i;
            }
            co_return network::peer_session::outbound_capacity;
        }, ::asio::use_future);
        REQUIRE(filled.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        return filled.get();
    }

    // One message the node wrote to the socket: its command, and for a
    // getheaders the first hash of its locator.
    struct sent_message {
        std::string command;
        std::optional<hash_digest> locator;
    };

    // Every message the node writes from now until nothing more arrives for
    // `quiet`.
    std::vector<sent_message> sent_until_quiet(std::chrono::milliseconds quiet) {
        std::vector<sent_message> out;
        while (true) {
            data_chunk heading(24);
            if ( ! read_exact(heading, std::chrono::steady_clock::now() + quiet)) return out;
            char command[13] = {};
            std::memcpy(command, heading.data() + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, heading.data() + 16, 4);
            data_chunk payload(length);
            if (length > 0 && ! read_exact(payload, std::chrono::steady_clock::now() + quiet)) return out;

            sent_message message{command, std::nullopt};
            if (message.command == domain::message::get_headers::command) {
                byte_reader reader(payload);
                auto parsed = domain::message::get_headers::from_data(reader, session->negotiated_version());
                REQUIRE(parsed.has_value());
                REQUIRE_FALSE(parsed->start_hashes().empty());
                message.locator = parsed->start_hashes().front();
            }
            out.push_back(std::move(message));
        }
    }

    ~wire_peer() {
        session->stop();
        std::error_code ignored;
        client.close(ignored);
        guard.reset();
        ioc.stop();
        if (runner.joinable()) runner.join();
    }

    wire_peer(wire_peer const&) = delete;
    wire_peer& operator=(wire_peer const&) = delete;

    // The complete locator of the next getheaders on the wire.
    // Nothing when none arrives in time.
    std::optional<hash_list> next_locator(std::chrono::milliseconds limit) {
        auto const deadline = std::chrono::steady_clock::now() + limit;
        while (true) {
            data_chunk heading(24);
            if ( ! read_exact(heading, deadline)) return std::nullopt;

            char command[13] = {};
            std::memcpy(command, heading.data() + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, heading.data() + 16, 4);

            data_chunk payload(length);
            if (length > 0 && ! read_exact(payload, deadline)) return std::nullopt;

            if (std::string(command) != domain::message::get_headers::command) {
                continue;
            }
            byte_reader reader(payload);
            auto parsed = domain::message::get_headers::from_data(reader, session->negotiated_version());
            REQUIRE(parsed.has_value());
            REQUIRE_FALSE(parsed->start_hashes().empty());
            return parsed->start_hashes();
        }
    }


    std::optional<hash_digest> next_getheaders(std::chrono::milliseconds limit) {
        auto const locator = next_locator(limit);
        if ( ! locator) return std::nullopt;
        return locator->front();
    }

    bool read_exact(data_chunk& out, std::chrono::steady_clock::time_point deadline) {
        while (client.available() < out.size()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ::asio::read(client, ::asio::buffer(out));
        return true;
    }

    // What arrives from this peer, through the read loop's own entry point.
    network::raw_message message(std::string const& command, data_chunk payload) const {
        network::raw_message raw;
        raw.heading = domain::message::heading(0xdab5bffa, command, uint32_t(payload.size()), 0);
        raw.payload = std::move(payload);
        return raw;
    }

    network::raw_message headers(std::vector<domain::chain::block> const& blocks) const {
        std::vector<domain::chain::header> elements;
        for (auto const& blk : blocks) {
            elements.push_back(blk.header());
        }
        return raw_headers(elements);
    }

    // Headers as given, whether or not they belong to any block.
    network::raw_message raw_headers(std::vector<domain::chain::header> const& carried) const {
        domain::message::header::list elements(carried.begin(), carried.end());
        auto built = domain::message::headers::create(std::move(elements));
        REQUIRE(built.has_value());
        auto const version = session->negotiated_version();
        data_chunk out(built->serialized_size(version));
        byte_writer writer(out);
        REQUIRE(built->to_data(writer, version).has_value());
        return message(domain::message::headers::command, std::move(out));
    }

    network::raw_message compact_block(domain::chain::block const& blk) const {
        domain::message::compact_block built(blk.header(), 7u, {}, {});
        auto const version = session->negotiated_version();
        data_chunk out(built.serialized_size(version));
        byte_writer writer(out);
        REQUIRE(built.to_data(writer, version).has_value());
        return message(domain::message::compact_block::command, std::move(out));
    }

    network::raw_message inventory(hash_digest const& block_hash) const {
        using domain::message::inventory_vector;
        auto built = domain::message::inventory::create(
            {{inventory_vector::type_id::block, block_hash}});
        REQUIRE(built.has_value());
        auto const version = session->negotiated_version();
        data_chunk out(built->serialized_size(version));
        byte_writer writer(out);
        REQUIRE(built->to_data(writer, version).has_value());
        return message(domain::message::inventory::command, std::move(out));
    }
};

// A chain whose headers run to `headers_to` and whose bodies are connected up
// to `bodies_to`, plus the block that comes next and is not in it.
struct staged_chain {
    std::vector<domain::chain::block> trunk;
    domain::chain::block next;
};

staged_chain stage(chain_fixture& fixture, uint32_t headers_to, uint32_t bodies_to) {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (headers_to + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= headers_to; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == headers_to);
    persist_headers(fixture, trunk, 1);

    // With nothing connected at all the coordinator refuses any range for an
    // unrelated and correct reason (#667).
    std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + bodies_to);
    connect_bodies(fixture, connected, 1);

    auto next = mine_block(prev, headers_to + 1,
        base_time + (headers_to + 1) * block_spacing, 0, {}, 0);
    REQUIRE(fixture.organizer().index().find(next.hash()) == blockchain::header_index::null_index);
    return {std::move(trunk), std::move(next)};
}

// The node and its orchestrator, started and torn down in the order the
// orchestrator can actually finish in.
struct node_run {
    network::settings const config;     // p2p_node keeps a reference
    p2p_node network;
    std::future<void> orchestrator;
    bool stopped{false};
    // What was connected, so a teardown that skips the explicit stop — a
    // REQUIRE failing mid-case — still ends them, as the connection manager
    // would on a real node. These sessions are not the manager's.
    std::vector<peer_ptr> connected;

    // With the node's own waits.
    node_run(chain_fixture& fixture, std::string const& tag)
        : node_run(fixture, tag, node_header_walk_timing) {}

    // With the header walk's waits given, so a control about them takes
    // milliseconds instead of the node's ten seconds.
    node_run(chain_fixture& fixture, std::string const& tag, header_walk_timing timing)
        : config(node_settings(tag))
        , network(config) {
        auto started = ::asio::co_spawn(network.thread_pool().get_executor(),
            [this]() -> ::asio::awaitable<int> {
                auto const ec = co_await network.start();
                co_return ec.value();
            }, ::asio::use_future);
        REQUIRE(started.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        REQUIRE(started.get() == 0);

        orchestrator = ::asio::co_spawn(network.thread_pool().get_executor(),
            sync_orchestrator(fixture.chain(), fixture.organizer(), network,
                domain::config::network::regtest,
                [](std::string const& reason) {
                    spdlog::error("[test] fatal reported: {}", reason);
                },
                timing),
            ::asio::use_future);
    }

    ~node_run() {
        if ( ! stopped) {
            (void)stop_with(connected, std::chrono::seconds(120));
        }
    }

    void connect(peer_ptr const& peer) {
        connected.push_back(peer);
        REQUIRE(wait_until([&] {
            return network.peer_events().try_send(std::error_code{},
                peer_notification{peer, peer_event_type::connected});
        }, std::chrono::seconds(10)));
    }

    void deliver(peer_ptr const& peer, network::raw_message const& raw) {
        (void)network.observe_block_message(peer, raw);
    }

    // The peers first, so the block download waiting on them completes, then
    // the node: these sessions are not the connection manager's, so stopping
    // the node does not end them. Answers whether the orchestrator finished
    // within `limit`.
    bool stop_with(std::vector<peer_ptr> const& peers, std::chrono::seconds limit) {
        for (auto const& peer : peers) {
            peer->stop();
        }
        return stop(limit);
    }

    // Answers whether the orchestrator finished within `limit`.
    bool stop(std::chrono::seconds limit) {
        stopped = true;
        network.stop();
        if (orchestrator.wait_for(limit) != std::future_status::ready) {
            return false;
        }
        orchestrator.get();
        return true;
    }
};

constexpr auto quick = std::chrono::milliseconds(10000);
constexpr auto quiet = std::chrono::milliseconds(1500);

} // namespace

TEST_CASE("integrated: confirming the tip opens the first block range, exactly once",
          "[header_tip][integration]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    chain_fixture fixture{"tip_blocksync"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Headers five ahead of the two connected bodies: block sync has
    // something to open once the tip is confirmed.
    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();

    auto const peer_config = node_settings("tip_blocksync_peer");
    wire_peer peer(peer_config, 9001, 5);

    std::optional<hash_digest> asked;
    std::optional<hash_digest> asked_again;
    bool opened = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_blocksync");
        run.connect(peer.session);

        // The stimulus goes out, from the tip.
        asked = peer.next_getheaders(quick);
        REQUIRE(asked.has_value());
        CHECK(*asked == tip_hash);

        // The peer is at the same tip, and answers with headers we already
        // hold — what the wire cannot tell apart from progress.
        run.deliver(peer.session, peer.headers(staged.trunk));

        opened = wait_until([&] { return log.count("block_range_request") > 0; }, quick);

        // Nobody is asked again: the walk is over and nothing polls.
        asked_again = peer.next_getheaders(quiet);

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("tip_blocksync");

    CHECK(stopped_cleanly);
    CHECK(opened);
    CHECK_FALSE(asked_again.has_value());

    // Exactly once, and opened BY the confirmation.
    CHECK(log.count("block_range_request") == 1u);
    CHECK(log.count("confirming the tip") == 1u);
    auto const confirmed = log.first_index("confirming the tip");
    auto const completed = log.first_index("Header sync COMPLETE");
    auto const ranged = log.first_index("block_range_request");
    REQUIRE(confirmed != std::string::npos);
    REQUIRE(completed != std::string::npos);
    REQUIRE(ranged != std::string::npos);
    CHECK(confirmed < completed);
    CHECK(completed < ranged);

    // A peer at the same tip is not accused of anything.
    CHECK(log.count("Peer error") == 0u);
}

TEST_CASE("integrated: a new header after the tip is confirmed opens its body range",
          "[header_tip][integration][announcements]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // Fully synced: headers and bodies level at 5. The tip is confirmed with
    // nothing owed to the bodies. Then a cmpctblock brings block 6's header:
    // it is taken in, its body range is opened, and the walk follows up from
    // the new tip.
    chain_fixture fixture{"tip_new_header"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 5);
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_new_header_peer");
    wire_peer peer(peer_config, 9101, 5);

    bool confirmed = false;
    size_t ranges_before = 0;
    bool taken_in = false;
    bool ranged = false;
    std::optional<hash_digest> follow_up;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_new_header");
        run.connect(peer.session);

        REQUIRE(peer.next_getheaders(quick).has_value());
        run.deliver(peer.session, peer.headers({}));
        confirmed = wait_until([&] { return log.count("Fully synced at height 5") > 0; }, quick);

        // Nothing is owed yet, so nothing was opened: the range that follows
        // can only come from the new header.
        ranges_before = log.count("block_range_request");

        run.deliver(peer.session, peer.compact_block(staged.next));

        taken_in = wait_until([&] {
            return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
        }, quick);
        ranged = wait_until([&] { return log.count("block_range_request 6-6") > 0; }, quick);

        // The walk asks again, from the tip the new header made.
        follow_up = peer.next_getheaders(quick);

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("tip_new_header");

    CHECK(stopped_cleanly);
    CHECK(confirmed);
    CHECK(ranges_before == 0u);
    CHECK(taken_in);
    CHECK(ranged);
    REQUIRE(follow_up.has_value());
    CHECK(*follow_up == next_hash);
}

TEST_CASE("integrated: an announcement during a stimulus is followed up after it",
          "[header_tip][integration][announcements]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // P is asked from tip 5. While that stimulus is out, an inv announces
    // block 6. P then answers the stimulus it was sent — with the five we
    // already hold. That answer predates the announcement, so the tip is NOT
    // confirmed on it: a follow-up goes out, from the tip, and brings 6 in.
    chain_fixture fixture{"tip_debt"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_debt_peer");
    wire_peer peer(peer_config, 9201, 5);

    std::optional<hash_digest> first;
    bool owed = false;
    std::optional<hash_digest> second;
    size_t confirmed_before_follow_up = 0;
    bool taken_in = false;
    std::optional<hash_digest> third;
    bool confirmed_at_end = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_debt");
        run.connect(peer.session);

        first = peer.next_getheaders(quick);
        REQUIRE(first.has_value());

        run.deliver(peer.session, peer.inventory(next_hash));
        owed = wait_until([&] { return log.count("unknown; a follow-up from height 5 is owed") > 0; }, quick);
        REQUIRE(owed);

        // The answer to the stimulus sent before the announcement.
        run.deliver(peer.session, peer.headers(staged.trunk));

        second = peer.next_getheaders(quick);
        confirmed_before_follow_up = log.count("confirming the tip");

        // What a peer asked from tip 5 answers: the one header past it.
        run.deliver(peer.session, peer.headers({staged.next}));
        taken_in = wait_until([&] {
            return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
        }, quick);

        third = peer.next_getheaders(quick);
        run.deliver(peer.session, peer.headers({}));
        confirmed_at_end = wait_until([&] { return log.count("confirming the tip") > 0; }, quick);

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("tip_debt");

    CHECK(stopped_cleanly);
    CHECK(*first == tip_hash);
    REQUIRE(second.has_value());
    CHECK(*second == tip_hash);
    CHECK(confirmed_before_follow_up == 0u);
    CHECK(taken_in);
    REQUIRE(third.has_value());
    CHECK(*third == next_hash);
    CHECK(confirmed_at_end);
}

TEST_CASE("integrated: without the announcement the same answer confirms the tip",
          "[header_tip][integration][announcements]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // The negation of the case above, on the same path: no inv, and the same
    // answer confirms the tip with no second getheaders.
    chain_fixture fixture{"tip_no_debt"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);

    auto const peer_config = node_settings("tip_no_debt_peer");
    wire_peer peer(peer_config, 9301, 5);

    bool confirmed = false;
    std::optional<hash_digest> second;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_no_debt");
        run.connect(peer.session);

        REQUIRE(peer.next_getheaders(quick).has_value());
        run.deliver(peer.session, peer.headers(staged.trunk));
        confirmed = wait_until([&] { return log.count("confirming the tip") > 0; }, quick);
        second = peer.next_getheaders(quiet);

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("tip_no_debt");

    CHECK(stopped_cleanly);
    CHECK(confirmed);
    CHECK_FALSE(second.has_value());
}

TEST_CASE("integrated: progress from a peer that was not asked is adopted, and a stale answer is not",
          "[header_tip][integration]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // P is asked. Q, never asked, sends block 6's header: progress, adopted.
    // Then P answers its stimulus with the five we held before — which, from
    // the peer being asked, ends its stimulus. The tip is not confirmed on it:
    // the walk restarted at 6, and Q, who moved it, is asked next, from 6.
    chain_fixture fixture{"tip_two_peers"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_two_peers_peer");
    wire_peer p(peer_config, 9401, 6);
    wire_peer q(peer_config, 9402, 6);

    std::optional<hash_digest> p_first;
    bool taken_in = false;
    std::optional<hash_digest> q_asked;
    std::optional<hash_digest> p_again;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_two_peers");
        run.connect(p.session);
        p_first = p.next_getheaders(quick);
        REQUIRE(p_first.has_value());
        run.connect(q.session);

        run.deliver(q.session, q.headers({staged.next}));
        taken_in = wait_until([&] {
            return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
        }, quick);
        REQUIRE(taken_in);

        run.deliver(p.session, p.headers(staged.trunk));
        q_asked = q.next_getheaders(quick);
        p_again = p.next_getheaders(quiet);

        stopped_cleanly = run.stop_with({p.session, q.session}, std::chrono::seconds(120));
    }
    log.dump("tip_two_peers");

    CHECK(stopped_cleanly);
    REQUIRE(q_asked.has_value());
    CHECK(*q_asked == next_hash);
    CHECK_FALSE(p_again.has_value());
    CHECK(log.count("confirming the tip") == 0u);
    CHECK(log.count("Peer error") == 0u);
}

TEST_CASE("integrated: a header whose parent we lack is followed up, not scored",
          "[header_tip][integration][announcements]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // Fully synced at 5. A peer announces block 7 while we have not heard of
    // block 6. That is missing information — the usual order of events when
    // announcements cross — and not evidence of an invalid chain: nothing is
    // reported against the peer, and a request from our tip fills the gap.
    chain_fixture fixture{"tip_orphan"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 5);
    auto const tip_hash = staged.trunk.back().hash();
    auto const after = mine_block(staged.next.hash(), 7,
        staged.next.header().timestamp() + block_spacing, 0, {}, 0);

    auto const peer_config = node_settings("tip_orphan_peer");
    wire_peer peer(peer_config, 9601, 5);

    bool confirmed = false;
    bool owed = false;
    std::optional<hash_digest> follow_up;
    bool filled = false;
    std::optional<int> score;
    std::optional<int> control_score;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_orphan");
        run.connect(peer.session);

        REQUIRE(peer.next_getheaders(quick).has_value());
        run.deliver(peer.session, peer.headers({}));
        confirmed = wait_until([&] { return log.count("confirming the tip") > 0; }, quick);

        run.deliver(peer.session, peer.compact_block(after));
        owed = wait_until([&] { return log.count("do not connect to the index") > 0; }, quick);

        follow_up = peer.next_getheaders(quick);
        run.deliver(peer.session, peer.headers({staged.next, after}));
        filled = wait_until([&] {
            return fixture.organizer().index().find(after.hash()) != blockchain::header_index::null_index;
        }, quick);

        auto const record = run.network.peer_db().get(peer.session->authority());
        score = record ? record->reputation_score : 0;

        // The same lookup sees a score when one is reported, so the zero above
        // is a reading and not a blind spot.
        (void)run.network.report_misbehavior(peer.session, 10, "control");
        auto const reported = run.network.peer_db().get(peer.session->authority());
        control_score = reported ? std::optional<int>{reported->reputation_score} : std::nullopt;

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("tip_orphan");

    CHECK(stopped_cleanly);
    CHECK(confirmed);
    CHECK(owed);
    REQUIRE(follow_up.has_value());
    CHECK(*follow_up == tip_hash);
    CHECK(filled);
    CHECK(log.count("Peer error") == 0u);
    REQUIRE(score.has_value());
    CHECK(*score == 0);
    REQUIRE(control_score.has_value());
    CHECK(*control_score == 10);
}

namespace {

size_t count_command(std::vector<wire_peer::sent_message> const& sent, std::string const& command) {
    return size_t(std::count_if(sent.begin(), sent.end(),
        [&](auto const& message) { return message.command == command; }));
}

size_t count_getheaders_from(std::vector<wire_peer::sent_message> const& sent, hash_digest const& hash) {
    return size_t(std::count_if(sent.begin(), sent.end(), [&](auto const& message) {
        return message.locator && *message.locator == hash;
    }));
}

} // namespace

TEST_CASE("integrated: a stimulus its peer's queue cannot take is never sent later",
          "[header_tip][integration][stimulus]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // P's outbound queue is full: its write loop is not draining it. The
    // stimulus to P from tip 5 waits for room, its deadline runs out, and the
    // walk withdraws it and moves to Q, whose header 6 moves the tip. When P's
    // queue drains, what P is sent is what its queue had taken, plus whatever
    // the walk asks it from the tip as it stands THEN: never the getheaders
    // from 5, which was withdrawn before the queue ever took it.
    chain_fixture fixture{"tip_backed_up"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_backed_up_peer");
    wire_peer p(peer_config, 9701, 6, true);
    wire_peer q(peer_config, 9702, 6);
    auto const filled = p.fill_outbound();
    REQUIRE(filled > 0u);

    constexpr header_walk_timing short_deadline{
        std::chrono::milliseconds(300),
        std::chrono::milliseconds(60000),
        std::chrono::milliseconds(60000)};

    bool timed_out = false;
    std::optional<hash_digest> q_asked;
    std::optional<hash_digest> q_asked_again;
    std::vector<wire_peer::sent_message> p_sent;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_backed_up", short_deadline);
        run.connect(p.session);
        timed_out = wait_until([&] { return log.count("reached its deadline") > 0; }, quick);

        run.connect(q.session);
        q_asked = q.next_getheaders(quick);
        run.deliver(q.session, q.headers({staged.next}));
        q_asked_again = q.next_getheaders(quick);

        // Room appears on P.
        p.start();
        p_sent = p.sent_until_quiet(quiet);

        stopped_cleanly = run.stop_with({p.session, q.session}, std::chrono::seconds(120));
    }
    log.dump("tip_backed_up");

    CHECK(stopped_cleanly);
    CHECK(timed_out);
    REQUIRE(q_asked.has_value());
    CHECK(*q_asked == tip_hash);
    REQUIRE(q_asked_again.has_value());
    CHECK(*q_asked_again == next_hash);
    CHECK(count_command(p_sent, domain::message::ping::command) == filled);
    // The withdrawn stimulus, from 5, never leaves.
    CHECK(count_getheaders_from(p_sent, tip_hash) == 0u);
}

TEST_CASE("integrated: a stimulus the walk parks behind is withdrawn too",
          "[header_tip][integration][stimulus]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone.
    captured_log log;

    // P is the only peer and its queue is full. Its stimulus waits for room
    // until the deadline; with nobody else to ask, the walk parks, its retry a
    // minute away. Nothing replaces the stimulus — so ending the walk's active
    // stimulus is what has to withdraw it. When P's queue then drains, no
    // getheaders goes out.
    chain_fixture fixture{"tip_park_withdraws"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();

    auto const peer_config = node_settings("tip_park_withdraws_peer");
    wire_peer p(peer_config, 9761, 6, true);
    auto const filled = p.fill_outbound();
    REQUIRE(filled > 0u);

    constexpr header_walk_timing short_deadline{
        std::chrono::milliseconds(300),
        std::chrono::milliseconds(60000),
        std::chrono::milliseconds(60000)};

    bool parked = false;
    std::vector<wire_peer::sent_message> p_sent;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_park_withdraws", short_deadline);
        run.connect(p.session);
        parked = wait_until([&] { return log.count("retrying in 60000ms") > 0; }, quick);
        REQUIRE(parked);

        p.start();
        p_sent = p.sent_until_quiet(quiet);

        stopped_cleanly = run.stop_with({p.session}, std::chrono::seconds(120));
    }
    log.dump("tip_park_withdraws");

    CHECK(stopped_cleanly);
    CHECK(count_command(p_sent, domain::message::ping::command) == filled);
    CHECK(count_getheaders_from(p_sent, tip_hash) == 0u);
    CHECK(count_command(p_sent, domain::message::get_headers::command) == 0u);
}

TEST_CASE("integrated: every queue full, then room: the waiting stimulus goes out and the debt is followed up",
          "[header_tip][integration][stimulus]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // P is the only peer and its outbound queue is full. The stimulus to P
    // waits for room; block 6 is announced meanwhile, and a follow-up is owed.
    // Nothing connects, disconnects or polls. Room returning on P is the event:
    // the waiting stimulus goes out, P answers with 6, the walk asks again from
    // 6 — the follow-up the debt needed — and 6 is in the index.
    chain_fixture fixture{"tip_all_full"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_all_full_peer");
    wire_peer p(peer_config, 9751, 5, true);
    auto const filled = p.fill_outbound();
    REQUIRE(filled > 0u);

    bool owed = false;
    size_t peer_updates_before_room = 0;
    std::optional<hash_digest> asked;
    bool taken_in = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_all_full");
        run.connect(p.session);
        REQUIRE(wait_until([&] { return log.count("Header stimulus 1 (peers)") > 0; }, quick));

        run.deliver(p.session, p.inventory(next_hash));
        owed = wait_until([&] { return log.count("a follow-up from height 5 is owed") > 0; }, quick);
        REQUIRE(owed);
        peer_updates_before_room = log.count("Header peers updated");

        // Room on P, and nothing else.
        p.start();
        asked = p.next_getheaders(quick);
        if (asked) {
            run.deliver(p.session, p.headers({staged.next}));
            taken_in = wait_until([&] {
                return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
            }, quick);
        }
        CHECK(log.count("Header peers updated") == peer_updates_before_room);

        stopped_cleanly = run.stop_with({p.session}, std::chrono::seconds(120));
    }
    log.dump("tip_all_full");

    CHECK(stopped_cleanly);
    CHECK(peer_updates_before_room == 1u);
    REQUIRE(asked.has_value());
    CHECK(*asked == tip_hash);
    CHECK(taken_in);
}

TEST_CASE("integrated: a valid prefix from a peer not being asked restarts a parked walk",
          "[header_tip][integration][prefix]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // Both peers were asked and neither answered in time: the walk is parked,
    // its retry a minute away. Q, not being asked, sends block 6's header and
    // an invalid one after it. The prefix moves the tip; the walk restarts
    // from it and asks P, without waiting for anything else to happen; and Q
    // is reported for the invalid header exactly as before.
    chain_fixture fixture{"tip_prefix"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const next_hash = staged.next.hash();
    auto const& next_header = staged.next.header();
    // Builds on 6 but with the wrong difficulty: rejected, and not for a
    // missing parent.
    domain::chain::header const invalid{next_header.version(), next_hash, next_header.merkle(),
        next_header.timestamp() + block_spacing, 0x1d00ffffu, 0u};

    auto const peer_config = node_settings("tip_prefix_peer");
    wire_peer p(peer_config, 9801, 7);
    wire_peer q(peer_config, 9802, 7);

    constexpr header_walk_timing short_deadline{
        std::chrono::milliseconds(300),
        std::chrono::milliseconds(60000),
        std::chrono::milliseconds(60000)};

    bool parked = false;
    bool taken_in = false;
    std::optional<hash_digest> p_asked;
    std::optional<hash_digest> p_asked_again;
    bool reported = false;
    std::optional<int> q_score;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_prefix", short_deadline);
        run.connect(p.session);
        REQUIRE(p.next_getheaders(quick).has_value());
        run.connect(q.session);
        REQUIRE(q.next_getheaders(quick).has_value());

        // Neither answered, nobody is left, and nothing is evidence.
        parked = wait_until([&] { return log.count("retrying in 60000ms") > 0; }, quick);
        REQUIRE(parked);

        run.deliver(q.session, q.raw_headers({next_header, invalid}));
        taken_in = wait_until([&] {
            return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
        }, quick);

        p_asked = p.next_getheaders(quick);
        p_asked_again = p.next_getheaders(quiet);

        reported = wait_until([&] { return log.count("Peer error") > 0; }, quick);
        auto const record = run.network.peer_db().get(q.session->authority());
        q_score = record ? std::optional<int>{record->reputation_score} : std::nullopt;

        stopped_cleanly = run.stop_with({p.session, q.session}, std::chrono::seconds(120));
    }
    log.dump("tip_prefix");

    CHECK(stopped_cleanly);
    CHECK(taken_in);
    REQUIRE(p_asked.has_value());
    CHECK(*p_asked == next_hash);           // from the tip the prefix made
    CHECK_FALSE(p_asked_again.has_value()); // and once
    CHECK(reported);
    CHECK(log.count("Peer error") == 1u);
    REQUIRE(q_score.has_value());
    CHECK(*q_score > 0);
    CHECK(log.count("confirming the tip") == 0u);
}

// -----------------------------------------------------------------------------
// Every peer timing out
// -----------------------------------------------------------------------------

namespace {

// Short waits, so these cases are about the walk and not about the clock.
constexpr header_walk_timing quick_walk{
    std::chrono::milliseconds(300),         // a peer's time to answer
    std::chrono::milliseconds(400),         // first retry of a parked walk
    std::chrono::milliseconds(3200)};

std::optional<int> reputation(node_run& run, peer_ptr const& peer) {
    auto const record = run.network.peer_db().get(peer->authority());
    return record ? std::optional<int>{record->reputation_score} : std::optional<int>{0};
}

} // namespace

TEST_CASE("integrated: every peer timing out is retried, and the answer is taken",
          "[header_tip][integration][retry]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // P and Q stay connected and claim 6; we hold 5. Both are asked and
    // neither answers in time. Nothing else happens: no peer comes or goes, no
    // block is announced. The walk's own retry asks P again, P answers this
    // time, and block 6 is taken in.
    chain_fixture fixture{"tip_all_timeout"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();
    auto const next_hash = staged.next.hash();

    auto const peer_config = node_settings("tip_all_timeout_peer");
    wire_peer p(peer_config, 9901, 6);
    wire_peer q(peer_config, 9902, 6);

    std::optional<hash_digest> p_first;
    std::optional<hash_digest> q_first;
    bool retry_armed = false;
    size_t peer_updates = 0;
    std::optional<hash_digest> p_again;
    bool taken_in = false;
    std::optional<int> p_score;
    std::optional<int> q_score;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_all_timeout", quick_walk);
        run.connect(p.session);
        run.connect(q.session);
        REQUIRE(wait_until([&] { return log.count("Header peers updated") >= 2; }, quick));
        peer_updates = log.count("Header peers updated");

        p_first = p.next_getheaders(quick);                 // and P says nothing
        q_first = q.next_getheaders(quick);                 // nor does Q
        retry_armed = wait_until([&] { return log.count("none left to ask; retrying in") > 0; }, quick);

        p_again = p.next_getheaders(quick);                 // the retry's stimulus
        if (p_again) {
            run.deliver(p.session, p.headers({staged.next}));
            taken_in = wait_until([&] {
                return fixture.organizer().index().find(next_hash) != blockchain::header_index::null_index;
            }, quick);
        }
        CHECK(log.count("Header peers updated") == peer_updates);
        p_score = reputation(run, p.session);
        q_score = reputation(run, q.session);

        stopped_cleanly = run.stop_with({p.session, q.session}, std::chrono::seconds(120));
    }
    log.dump("tip_all_timeout");

    CHECK(stopped_cleanly);
    REQUIRE(p_first.has_value());
    REQUIRE(q_first.has_value());
    CHECK(retry_armed);
    REQUIRE(p_again.has_value());
    CHECK(*p_again == tip_hash);
    CHECK(taken_in);
    // Not confirmed on silence, and nobody scored for being slow.
    CHECK(log.count("confirming the tip") == 0u);
    CHECK(log.count("Peer error") == 0u);
    CHECK(p_score == std::optional<int>{0});
    CHECK(q_score == std::optional<int>{0});
}

TEST_CASE("integrated: peers that never answer are retried at a falling rate, unscored",
          "[header_tip][integration][retry]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // Nobody ever answers. The walk keeps trying — it must, nothing else will
    // move it — but each retry waits twice as long as the last, up to the cap:
    // bounded, not a hot loop, and not a reason to report anybody.
    chain_fixture fixture{"tip_never_answer"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    (void)stage(fixture, 5, 2);

    auto const peer_config = node_settings("tip_never_answer_peer");
    wire_peer p(peer_config, 9911, 6);

    constexpr header_walk_timing slow_down{
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(800)};

    size_t asked = 0;
    std::optional<int> score;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_never_answer", slow_down);
        run.connect(p.session);

        // Four seconds of silence. Each round is 100ms of deadline plus the
        // retry delay: 200, 400, 800, 800, 800... — about seven rounds.
        auto const until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (std::chrono::steady_clock::now() < until) {
            if (p.next_getheaders(std::chrono::milliseconds(100))) {
                ++asked;
            }
        }
        score = reputation(run, p.session);
        stopped_cleanly = run.stop_with({p.session}, std::chrono::seconds(120));
    }
    log.dump("tip_never_answer");

    CHECK(stopped_cleanly);
    CHECK(asked >= 3u);             // it does keep trying
    CHECK(asked <= 10u);            // at a rate the backoff sets, not as fast as it can
    // The delay grew and stopped at the cap.
    CHECK(log.count("retrying in 200ms (attempt 1)") == 1u);
    CHECK(log.count("retrying in 400ms (attempt 2)") == 1u);
    CHECK(log.count("retrying in 800ms") >= 1u);
    CHECK(log.count("retrying in 1600ms") == 0u);
    CHECK(log.count("confirming the tip") == 0u);
    CHECK(log.count("Peer error") == 0u);
    CHECK(score == std::optional<int>{0});
}

TEST_CASE("integrated: shutdown with a retry armed ends at once",
          "[header_tip][integration][retry][shutdown]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // The walk is parked with a retry a minute away. Stopping the node does
    // not wait for it, and the retry is never delivered.
    chain_fixture fixture{"tip_retry_shutdown"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    (void)stage(fixture, 5, 2);

    auto const peer_config = node_settings("tip_retry_shutdown_peer");
    wire_peer p(peer_config, 9921, 6);

    constexpr header_walk_timing long_retry{
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(60000),
        std::chrono::milliseconds(60000)};

    bool armed = false;
    bool stopped_cleanly = false;
    std::chrono::steady_clock::duration took{};
    {
        node_run run(fixture, "tip_retry_shutdown", long_retry);
        run.connect(p.session);
        REQUIRE(p.next_getheaders(quick).has_value());
        armed = wait_until([&] { return log.count("retrying in 60000ms") > 0; }, quick);

        // The peer goes too, as on a real node, so nothing unrelated to the
        // retry — a body download waiting on it — is what is being timed.
        auto const started = std::chrono::steady_clock::now();
        stopped_cleanly = run.stop_with({p.session}, std::chrono::seconds(120));
        took = std::chrono::steady_clock::now() - started;
    }
    log.dump("tip_retry_shutdown");

    CHECK(armed);
    CHECK(stopped_cleanly);
    CHECK(took < std::chrono::seconds(8));
    CHECK(log.count("[sync_coordinator] Header stimulus") == 1u);   // the retry never ran
}

TEST_CASE("integrated: shutdown does not wait out a stimulus deadline",
          "[header_tip][integration][shutdown]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // A stimulus is out and its peer never answers: ten seconds of deadline
    // are left when the node stops. The orchestrator finishes long before.
    chain_fixture fixture{"tip_shutdown"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    (void)stage(fixture, 5, 2);

    auto const peer_config = node_settings("tip_shutdown_peer");
    wire_peer peer(peer_config, 9501, 5);

    bool stopped_cleanly = false;
    std::chrono::steady_clock::duration took{};
    {
        node_run run(fixture, "tip_shutdown");
        run.connect(peer.session);
        REQUIRE(peer.next_getheaders(quick).has_value());

        auto const started = std::chrono::steady_clock::now();
        stopped_cleanly = run.stop(std::chrono::seconds(120));
        took = std::chrono::steady_clock::now() - started;
    }
    log.dump("tip_shutdown");

    CHECK(stopped_cleanly);
    CHECK(took < std::chrono::seconds(8));
    // The deadline did not fire on the way out.
    CHECK(log.count("reached its deadline") == 0u);
}

// -----------------------------------------------------------------------------
// A height nobody backs
// -----------------------------------------------------------------------------

namespace {

enum class claimer_answer { empty, silent };

struct claim_outcome {
    hash_digest tip_hash{};
    std::optional<hash_digest> honest_asked;
    std::optional<hash_digest> claimer_asked;
    bool confirmed{false};
    bool completed{false};
    std::optional<hash_digest> asked_again;
    bool stopped_cleanly{false};
};

// H claims the height we hold and answers with what we hold. C claims a height
// nobody has — in its version message, which nothing verifies — and when it is
// asked answers empty, or not at all. H is asked first, C after it, both from
// our tip.
claim_outcome run_unbacked_claim(captured_log& log, std::string const& tag, claimer_answer answer) {
    chain_fixture fixture{tag.c_str()};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Headers ahead of the bodies, so header sync has something to complete.
    auto const staged = stage(fixture, 5, 2);

    auto const peer_config = node_settings(tag + "_peer");
    wire_peer honest(peer_config, 9931, 5);
    wire_peer claimer(peer_config, 9932, 4'000'000'000u);

    // A silent peer's deadline is short; a retry is a minute away, so nothing
    // here can be the retry's doing.
    constexpr header_walk_timing timing{
        std::chrono::milliseconds(300),
        std::chrono::milliseconds(60000),
        std::chrono::milliseconds(60000)};

    claim_outcome out;
    out.tip_hash = staged.trunk.back().hash();
    {
        node_run run(fixture, tag, timing);
        run.connect(honest.session);
        out.honest_asked = honest.next_getheaders(quick);
        REQUIRE(out.honest_asked.has_value());

        // C joins while H is being asked, so it is in the walk before H ends.
        run.connect(claimer.session);
        REQUIRE(wait_until([&] { return log.count("Header peers updated: 2 connected") > 0; }, quick));

        run.deliver(honest.session, honest.headers(staged.trunk));
        out.claimer_asked = claimer.next_getheaders(quick);
        if (out.claimer_asked && answer == claimer_answer::empty) {
            run.deliver(claimer.session, claimer.headers({}));
        }

        out.confirmed = wait_until([&] { return log.count("confirming the tip") > 0; }, quick);
        out.completed = wait_until([&] { return log.count("Header sync COMPLETE") > 0; }, quick);

        // Nothing goes on asking on the claim's behalf.
        out.asked_again = honest.next_getheaders(quiet);

        out.stopped_cleanly = run.stop_with({honest.session, claimer.session}, std::chrono::seconds(120));
    }
    log.dump(tag);
    return out;
}

void check_unbacked_claim(captured_log const& log, claim_outcome const& out) {
    CHECK(out.stopped_cleanly);
    // Both were asked, from our tip: what follows is about their answers.
    REQUIRE(out.honest_asked.has_value());
    CHECK(*out.honest_asked == out.tip_hash);
    REQUIRE(out.claimer_asked.has_value());
    CHECK(*out.claimer_asked == out.tip_hash);

    // The tip is confirmed on H's answer, and header sync completes on it.
    // Completing is what opens the download below the checkpoint, where
    // nothing else may: regtest has no checkpoint, so here the bodies past it
    // open through the UTXO build as well, and a range proves nothing.
    CHECK(out.confirmed);
    CHECK(out.completed);
    CHECK(log.count("confirming the tip") == 1u);
    CHECK(log.count("Header sync COMPLETE") == 1u);
    CHECK(log.count("none left to ask") == 0u);
    CHECK_FALSE(out.asked_again.has_value());

    // An empty or missing answer is not misbehaviour: nobody is reported.
    CHECK(log.count("Peer error") == 0u);
}

} // namespace

TEST_CASE("integrated: a height claim nobody backs does not hold block sync, empty answer",
          "[header_tip][integration][claim]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone.
    captured_log log;

    check_unbacked_claim(log,
        run_unbacked_claim(log, "tip_claim_empty", claimer_answer::empty));
}

TEST_CASE("integrated: a height claim nobody backs does not hold block sync, no answer",
          "[header_tip][integration][claim]") {
    captured_log log;

    check_unbacked_claim(log,
        run_unbacked_claim(log, "tip_claim_silent", claimer_answer::silent));
}

// -----------------------------------------------------------------------------
// Evidence has to come after the debt
// -----------------------------------------------------------------------------

namespace {

// Which of `peers` the node sends a getheaders to next, and from where: read
// off each socket in turn until one arrives or `limit` passes.
std::optional<std::pair<size_t, hash_digest>> next_asked(std::vector<wire_peer*> const& peers,
                                                        std::chrono::milliseconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        for (size_t i = 0; i < peers.size(); ++i) {
            if (auto asked = peers[i]->next_getheaders(std::chrono::milliseconds(20))) {
                return std::pair{i, *asked};
            }
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("integrated: an answer from before an announcement does not confirm the tip after it",
          "[header_tip][integration][announcements][evidence]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone.
    captured_log log;

    // P, Q and R at our tip. P is asked and has nothing new. The next peer is
    // asked, and while that stimulus is out a block is announced. That peer
    // then answers the stimulus it was sent, with what we hold; the last peer
    // is asked after the announcement and never answers. Nothing anyone asked
    // after the announcement said is in, so the tip is not confirmed: the walk
    // parks, its retry asks again from the tip, and the answers to that confirm.
    chain_fixture fixture{"tip_debt_three"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const staged = stage(fixture, 5, 2);
    auto const tip_hash = staged.trunk.back().hash();

    auto const peer_config = node_settings("tip_debt_three_peer");
    wire_peer p(peer_config, 9941, 5);
    wire_peer q(peer_config, 9942, 5);
    wire_peer r(peer_config, 9943, 5);

    constexpr header_walk_timing timing{
        std::chrono::milliseconds(300),         // a peer's time to answer
        std::chrono::milliseconds(400),         // first retry of a parked walk
        std::chrono::milliseconds(3200)};

    std::optional<hash_digest> p_first;
    std::optional<std::pair<size_t, hash_digest>> before_debt;
    bool owed = false;
    std::optional<std::pair<size_t, hash_digest>> after_debt;
    bool retried = false;
    size_t confirmed_before_retry = 0;
    std::optional<hash_digest> p_again;
    bool confirmed_at_end = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "tip_debt_three", timing);
        run.connect(p.session);
        p_first = p.next_getheaders(quick);
        REQUIRE(p_first.has_value());

        // Q and R join while P is being asked.
        run.connect(q.session);
        run.connect(r.session);
        REQUIRE(wait_until([&] { return log.count("Header peers updated: 3 connected") > 0; }, quick));

        run.deliver(p.session, p.headers(staged.trunk));
        std::vector<wire_peer*> rest{&q, &r};
        before_debt = next_asked(rest, quick);
        REQUIRE(before_debt.has_value());
        auto& asked_before = *rest[before_debt->first];
        auto& asked_after = *rest[1 - before_debt->first];

        run.deliver(p.session, p.inventory(staged.next.hash()));
        owed = wait_until([&] { return log.count("unknown; a follow-up from height 5 is owed") > 0; }, quick);
        REQUIRE(owed);

        // The answer to the stimulus sent before the announcement.
        run.deliver(asked_before.session, asked_before.headers(staged.trunk));
        after_debt = next_asked({&asked_after}, quick);

        // And the last peer says nothing. The walk parks rather than confirm.
        retried = wait_until([&] { return log.count("none left to ask; retrying in") > 0; }, quick);
        confirmed_before_retry = log.count("confirming the tip");

        // The retry asks again from the tip, P first; answered after the
        // announcement, its answer counts.
        p_again = p.next_getheaders(quick);
        if (p_again) {
            run.deliver(p.session, p.headers(staged.trunk));
        }
        confirmed_at_end = wait_until([&] { return log.count("confirming the tip") > 0; }, quick);

        stopped_cleanly = run.stop_with({p.session, q.session, r.session}, std::chrono::seconds(120));
    }
    log.dump("tip_debt_three");

    CHECK(stopped_cleanly);
    REQUIRE(p_first.has_value());
    CHECK(*p_first == tip_hash);
    REQUIRE(before_debt.has_value());
    CHECK(before_debt->second == tip_hash);
    CHECK(owed);
    // Asked after the announcement, from the tip: the debt was carried out.
    REQUIRE(after_debt.has_value());
    CHECK(after_debt->second == tip_hash);

    CHECK(retried);
    CHECK(confirmed_before_retry == 0u);
    REQUIRE(p_again.has_value());
    CHECK(*p_again == tip_hash);
    CHECK(confirmed_at_end);
    CHECK(log.count("confirming the tip") == 1u);
    CHECK(log.count("Peer error") == 0u);
}


TEST_CASE("integrated: restart on an orphan tip locates a same-height peer fork",
          "[header_tip][integration][orphan_locator]") {
    captured_log log;
    chain_fixture fixture{"orphan_tip_locator"};
    REQUIRE(fixture.created());
    fixture.settings().max_checkpoint_height = 0;
    fixture.settings().max_reorg_depth = -1;
    REQUIRE(fixture.start());
    constexpr uint32_t common_height = kth::max_get_headers;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base = uint32_t(zulu_time()) - (common_height + 30) * block_spacing;
    std::vector<domain::chain::block> common;
    common.reserve(common_height);
    auto previous = genesis.hash();
    for (uint32_t height = 1; height <= common_height; ++height) {
        common.push_back(mine_block(previous, height, base + height * block_spacing, 0, {}, 0));
        previous = common.back().hash();
    }
    auto const orphan = mine_block(previous, common_height + 1,
        base + (common_height + 1) * block_spacing, 1, {}, 0);
    auto const sibling = mine_block(previous, common_height + 1,
        base + (common_height + 1) * block_spacing + 30, 2, {}, 0);
    auto const extension = mine_block(sibling.hash(), common_height + 2,
        base + (common_height + 2) * block_spacing + 30, 2, {}, 0);
    auto local = common;
    local.push_back(orphan);
    REQUIRE(fixture.organizer().add_headers(headers_of(local)).headers_added == local.size());
    persist_headers(fixture, local, 1);
    REQUIRE(fixture.restart());
    REQUIRE(fixture.organizer().header_tip_hash() == orphan.hash());
    REQUIRE(fixture.organizer().index().find(sibling.hash()) == blockchain::header_index::null_index);

    auto const peer_config = node_settings("orphan_tip_locator_peer");
    // Same advertised height, different tip: height claims cannot find the fork.
    wire_peer peer(peer_config, 9921, common_height + 1);
    std::optional<hash_list> first_locator;
    std::optional<hash_list> adopted_locator;
    bool learned_sibling = false;
    bool adopted = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "orphan_tip_locator");
        run.connect(peer.session);
        first_locator = peer.next_locator(quick);
        REQUIRE(first_locator);
        // Emulate the peer's normal locator matching: use the first known hash,
        // otherwise genesis. Its bounded response holds at most 2000 headers.
        uint32_t matched_height = 0;
        for (auto const& hash : *first_locator) {
            if (hash == sibling.hash()) { matched_height = common_height + 1; break; }
            if (hash == genesis.hash()) break;
            auto const known = std::find_if(common.begin(), common.end(), [&](auto const& block) {
                return block.hash() == hash;
            });
            if (known != common.end()) {
                matched_height = uint32_t(std::distance(common.begin(), known)) + 1;
                break;
            }
        }
        std::vector<domain::chain::block> reply;
        for (uint32_t height = matched_height + 1;
             height <= common_height + 1 && reply.size() < kth::max_get_headers; ++height) {
            reply.push_back(height <= common_height ? common[height - 1] : sibling);
        }
        run.deliver(peer.session, peer.headers(reply));
        learned_sibling = wait_until([&] {
            return fixture.organizer().index().find(sibling.hash()) != blockchain::header_index::null_index;
        }, quick);
        // Reproduce a getheaders message admitted before adoption but still
        // unread on the wire. Use the real session and its execution context.
        auto stale = ::asio::co_spawn(peer.ioc,
            [session = peer.session, tip = orphan.hash()]() -> ::asio::awaitable<code> {
                co_return co_await session->send(domain::message::get_headers{hash_list{tip}, null_hash});
            }, ::asio::use_future);
        auto const stale_ready = stale.wait_for(quick) == std::future_status::ready;
        CHECK(stale_ready);
        if (stale_ready) CHECK(stale.get() == error::success);
        // The sibling alone has equal work. Its later extension can be admitted
        // only if the initial locator let the peer supply that missing sibling.
        run.deliver(peer.session, peer.headers({extension}));
        adopted = wait_until([&] {
            return fixture.organizer().header_tip_hash() == extension.hash();
        }, quick);
        if (adopted) {
            auto const deadline = std::chrono::steady_clock::now() + quick;
            while (std::chrono::steady_clock::now() < deadline) {
                auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                auto next = peer.next_locator(remaining);
                if ( ! next) break;
                if ( ! next->empty() && next->front() == extension.hash()) {
                    adopted_locator = std::move(next);
                    break;
                }
            }
        }
        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    CHECK(stopped_cleanly);
    CHECK(learned_sibling);
    CHECK(adopted);
    REQUIRE(first_locator);
    CHECK(first_locator->front() == orphan.hash());
    CHECK(first_locator->back() == genesis.hash());
    CHECK(first_locator->size() <= kth::max_locator);
    CHECK(std::find(first_locator->begin(), first_locator->end(), common.back().hash()) != first_locator->end());
    REQUIRE(adopted_locator);
    REQUIRE(adopted_locator->size() >= 2);
    CHECK(adopted_locator->front() == extension.hash());
    CHECK((*adopted_locator)[1] == sibling.hash());
    CHECK(std::find(adopted_locator->begin(), adopted_locator->end(), orphan.hash()) == adopted_locator->end());
    auto const persisted = fixture.chain().get_header(common_height + 1);
    REQUIRE(persisted);
    CHECK(domain::chain::hash(*persisted) == sibling.hash());
}

// =============================================================================
// A block handed to the node at its tip (#563)
// =============================================================================

namespace {

// block_chain::organize on a thread of its own, as the RPC server calls it.
std::future<code> organize_async(chain_fixture& fixture, domain::chain::block const& block) {
    return std::async(std::launch::async, [&fixture, block] {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io,
            fixture.chain().organize(std::make_shared<domain::message::block const>(block)),
            ::asio::use_future);
        io.run();
        return result.get();
    });
}

code organize_now(chain_fixture& fixture, domain::chain::block const& block) {
    auto organized = organize_async(fixture, block);
    REQUIRE(organized.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
    return organized.get();
}

} // namespace

TEST_CASE("a block that does not pass at the connected tip is refused before anything is stored",
          "[header_tip][submit]") {
    // No orchestrator: everything here is decided by organize() itself, before
    // the block reaches anything that writes.
    chain_fixture fixture{"submit_refused"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const staged = stage(fixture, 5, 5);
    auto const unknown = [&](domain::chain::block const& block) {
        return fixture.organizer().index().find(block.hash()) == blockchain::header_index::null_index;
    };

    // The tip itself, and a block built on something below it.
    CHECK(organize_now(fixture, staged.trunk[4]) == error::duplicate_block);
    auto const off_tip = mine_block(staged.trunk[2].hash(), 4,
        staged.trunk[3].header().timestamp() + 1, /*salt*/ 3, {}, 0);
    CHECK(organize_now(fixture, off_tip) == error::orphan_block);
    CHECK(unknown(off_tip));

    // The next block's header over another block's transactions.
    auto const other = mine_block(staged.trunk[4].hash(), 6,
        staged.next.header().timestamp(), /*salt*/ 5, {}, 0);
    domain::chain::block const mismatched(staged.next.header(), other.transactions());
    CHECK(organize_now(fixture, mismatched) == error::merkle_mismatch);
    CHECK(unknown(mismatched));

    // A valid next block, with nothing running that connects blocks.
    CHECK(organize_now(fixture, staged.next) == error::service_stopped);
    CHECK(unknown(staged.next));

    auto const view = fixture.chain().chain_view();
    REQUIRE(view);
    CHECK(view->connected_tip_height == 5u);
}

TEST_CASE("integrated: a block handed to the node at its tip is connected, and no peer is asked for it",
          "[header_tip][integration][submit]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone: swapping the default logger under a
    // running thread is a data race.
    captured_log log;

    // Fully synced at 5. Block 6 is handed to the node, as a block a miner
    // found reaches it: it is connected through the node's own storage and
    // UTXO build, and the peer is never asked for a body only this node has.
    chain_fixture fixture{"submit_at_tip"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const staged = stage(fixture, 5, 5);

    auto const peer_config = node_settings("submit_at_tip_peer");
    wire_peer peer(peer_config, 9201, 5);

    bool confirmed = false;
    std::optional<code> accepted;
    std::optional<code> again;
    std::vector<wire_peer::sent_message> sent;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "submit_at_tip");
        run.connect(peer.session);

        REQUIRE(peer.next_getheaders(quick).has_value());
        run.deliver(peer.session, peer.headers({}));
        confirmed = wait_until([&] { return log.count("Fully synced at height 5") > 0; }, quick);

        accepted = organize_now(fixture, staged.next);
        // Handed over again once connected.
        again = organize_now(fixture, staged.next);
        sent = peer.sent_until_quiet(quiet);

        stopped_cleanly = run.stop_with({peer.session}, std::chrono::seconds(120));
    }
    log.dump("submit_at_tip");

    CHECK(stopped_cleanly);
    CHECK(confirmed);
    REQUIRE(accepted.has_value());
    CHECK(*accepted == error::success);
    REQUIRE(again.has_value());
    CHECK(*again == error::duplicate_block);

    auto const view = fixture.chain().chain_view();
    REQUIRE(view);
    CHECK(view->connected_tip_height == 6u);
    CHECK(view->tip_hash == staged.next.hash());

    // Its body came with it: no range was opened and nothing was asked for.
    CHECK(log.count("block_range_request") == 0u);
    CHECK(std::none_of(sent.begin(), sent.end(), [](wire_peer::sent_message const& message) {
        return message.command == domain::message::get_data::command;
    }));
}
