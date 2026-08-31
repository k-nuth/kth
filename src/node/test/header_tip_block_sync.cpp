// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "header_peer_harness.hpp"
#include "sync_harness.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/domain/message/version.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/orchestrator.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// The functional criterion of #705, through the real orchestrator
// =============================================================================
//
// Every other control in this suite drives one task. This one drives the whole
// sync system -- the real sync_orchestrator, the real coordinator, the real
// organizer over a real chain, and a peer whose answers are decided in advance
// -- because the thing that actually broke was none of those pieces on its own:
// it was that confirming the tip never happened, so block sync never started
// and the node issued no getdata for as long as it was left running.
//
// The chain here has the shape the wedged node had: headers ahead of bodies,
// and a peer that answers a locator at the header tip with headers the chain
// already holds. What must come out the other end is one block range, opened
// once.

namespace {

// Swap the default logger for one that keeps what was written. The coordinator
// reports the range it opens; that line is the observable.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        // Large enough that nothing these cases count can be evicted before
        // they count it. The coordinator's stop bridge logs four lines every
        // 100ms, so a run that spends a minute winding down writes thousands on
        // its own, and an exact count taken afterwards would be counting what
        // survived rather than what happened.
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(262144))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    // What the run actually said, for a failure that would otherwise report a
    // zero with no explanation.
    void dump() const {
        auto const path = std::filesystem::temp_directory_path() / "kth_tip_blocksync_dump.log";
        std::ofstream out(path, std::ios::trunc);
        for (auto const& line : sink->last_formatted()) {
            out << line;
        }
        UNSCOPED_INFO("orchestrator log written to " << path.string());
    }

    // The last line matching `needle`, for state that is only meaningful at the
    // end of the run.
    [[nodiscard]] std::string last(std::string_view needle) const {
        std::string found;
        for (auto const& line : sink->last_formatted()) {
            if (line.find(needle) != std::string::npos) found = line;
        }
        return found;
    }

    // Where a line first appears, so the test can say which event caused which.
    // npos when it never does.
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

network::settings peer_settings() {
    network::settings settings;
    settings.identifier = 0xdab5bffa;           // regtest
    settings.protocol_maximum = 70015;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;          // nothing dials out
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_tip_blocksync_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_tip_blocksync_banlist_nonexistent.dat";
    return settings;
}

void claim_height(peer_ptr const& peer, uint32_t height) {
    domain::message::version v;
    v.set_start_height(height);
    peer->set_peer_version(std::make_shared<domain::message::version const>(std::move(v)));
}

// The `headers` payload for headers the chain already holds, as the peer at the
// tip re-sends them.
data_chunk known_headers_payload(peer_ptr const& peer,
                                 std::vector<domain::chain::block> const& blocks) {
    domain::message::header::list elements;
    elements.reserve(blocks.size());
    for (auto const& blk : blocks) {
        elements.push_back(blk.header());
    }

    auto message = domain::message::headers::create(std::move(elements));
    REQUIRE(message.has_value());

    auto const version = peer->negotiated_version();
    data_chunk out(message->serialized_size(version));
    byte_writer writer(out);
    REQUIRE(message->to_data(writer, version).has_value());
    return out;
}

// An io_context run on a thread, stopped and joined by the destructor.
//
// REQUIRE throws. Without this, a failure anywhere below unwinds past the join
// and destroys a joinable std::thread, which calls std::terminate: the process
// dies, and the log dump this case exists to produce -- along with every CHECK
// after the failure -- is never seen. Exactly the shape of #669, in a test.
struct io_thread {
    ::asio::io_context& context;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard;
    std::thread runner;

    explicit io_thread(::asio::io_context& ioc)
        : context(ioc)
        , guard(::asio::make_work_guard(ioc))
        , runner([&ioc]() { ioc.run(); })
    {}

    ~io_thread() {
        guard.reset();
        context.stop();
        if (runner.joinable()) {
            runner.join();
        }
    }

    io_thread(io_thread const&) = delete;
    io_thread& operator=(io_thread const&) = delete;
};

// Wait for a condition, bounded, without sleeping through the whole budget:
// this returns the moment it is true. A failure costs the deadline, once.
bool wait_until(std::function<bool()> const& done, std::chrono::seconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return done();
}

} // namespace

TEST_CASE("integrated: confirming the tip opens the first block range, exactly once",
          "[header_tip][integration]") {
    chain_fixture fixture{"tip_blocksync"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Headers five ahead of bodies: the state the wedged node was in, and the
    // one where block sync has something to open.
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 35 * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 5);
    persist_headers(fixture, trunk, 1);

    // Bodies for the first two only. Headers ahead of bodies is the shape that
    // gives block sync something to open, and the UTXO set has to be able to
    // say how far it describes -- with nothing connected at all the coordinator
    // refuses the range for an unrelated and correct reason (#667), which would
    // make this case pass or fail on something other than what it is about.
    std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 2);
    connect_bodies(fixture, connected, 1);

    // The fake peer lives on its own context, run by its own thread: the
    // orchestrator runs on the network's pool and would otherwise be waiting on
    // a peer nobody is driving.
    ::asio::io_context peer_ioc;
    auto peer_config = peer_settings();
    auto peer = make_peer(peer_ioc, peer_config, 9001);
    claim_height(peer, 5);

    // Whatever it is asked, it answers with the five headers we already have --
    // which is what a peer at the same tip does, and what the wire cannot tell
    // apart from progress.
    for (int i = 0; i < 4; ++i) {
        network::raw_message response;
        response.payload = known_headers_payload(peer, trunk);
        REQUIRE(peer->headers_responses().try_send(std::error_code{}, std::move(response)));
    }

    io_thread const peer_runner{peer_ioc};

    captured_log log;

    // Named, and declared here on purpose: p2p_node keeps `settings const&`, so
    // a temporary would be dangling from the first member access onwards.
    auto const network_config = peer_settings();

    // The node lives in a scope of its own, so it is torn down before the peer's
    // io_context thread is joined below rather than during the case's unwind.
    bool opened = false;
    bool stopped_cleanly = false;
    {
    p2p_node network(network_config);

    // Futures, not detached coroutines. A detached coroutine that captures this
    // frame by reference can still be resumed on a pool thread after the frame
    // is gone -- ASan caught exactly that here, reading the flags this case used
    // to signal with. Waiting on the future is what guarantees the coroutine and
    // its frame are both finished before anything it referred to goes away.
    auto start_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        [&network]() -> ::asio::awaitable<int> {
            auto const ec = co_await network.start();
            co_return ec.value();
        }, ::asio::use_future);

    REQUIRE(start_result.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    REQUIRE(start_result.get() == 0);

    auto orchestrator_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        sync_orchestrator(fixture.chain(), fixture.organizer(), network,
            domain::config::network::regtest,
            [](std::string const& reason) {
                spdlog::error("[test] fatal reported: {}", reason);
            }),
        ::asio::use_future);

    // The peer arrives the way a real one does, through the node's own event
    // channel. Retried until it lands: the channel has no room until the
    // orchestrator's peer bridge is receiving on it, and a dropped notification
    // would leave the sync tasks with no peers and nothing to explain it.
    REQUIRE(wait_until([&]() {
        return network.peer_events().try_send(std::error_code{},
            peer_notification{peer, peer_event_type::connected});
    }, std::chrono::seconds(10)));

    // The whole point: a block range gets opened.
    opened = wait_until(
        [&]() { return log.count("block_range_request") > 0; },
        std::chrono::seconds(30));

    // Teardown in the order the orchestrator can actually finish in: the peer
    // goes down first so anything waiting on it completes, then the network,
    // and only then does this scope let the chain be destroyed -- the
    // orchestrator holds it by reference.
    // The peer leaves the way it arrived. Without this the peer_provider keeps
    // the session it was told about — a shared_ptr that outlives the case and
    // is reported as a leak — and the test would be modelling a lifecycle that
    // never happens on a real node, where a session always ends by
    // disconnecting.
    peer->stop();
    REQUIRE(wait_until([&]() {
        return network.peer_events().try_send(std::error_code{},
            peer_notification{peer, peer_event_type::disconnected});
    }, std::chrono::seconds(10)));

    REQUIRE(wait_until([&]() { return log.count("Peer disconnected") > 0; },
        std::chrono::seconds(10)));

    network.stop();
    stopped_cleanly =
        orchestrator_result.wait_for(std::chrono::seconds(120)) == std::future_status::ready;
    if (stopped_cleanly) {
        orchestrator_result.get();      // rethrows anything the orchestrator threw
    }

    }   // the node goes down here


    // A CHECK, not a REQUIRE: a run that did not wind down cleanly still has a
    // log worth reading, and aborting here would hide every finding below it.
    CHECK(stopped_cleanly);

    log.dump();   // always: an integration failure is unreadable without it
    CHECK(opened);

    // Exactly once. The coordinator has three doors onto the post-checkpoint
    // range and a confirmation that opened two would be a regression of #663.
    CHECK(log.count("block_range_request") == 1u);


    // And the retry storm is gone: a peer with nothing new is not a peer that
    // failed, so no request is re-issued at the same height with a new peer.
    CHECK(log.count("Retrying header sync") == 0u);

    // Nor is it accused. Every one of those retries also reported the peer to
    // the reputation system, and a non-bannable error still scores +10 against
    // it: ten reports reach the ban threshold, and on mainnet the peer that
    // answered with headers we already had was banned on the eleventh, for
    // being at the same tip as us.
    CHECK(log.count("Peer error") == 0u);

    // The tip really was confirmed rather than the range coming from somewhere
    // else, and it was confirmed once.
    CHECK(log.count("confirming the tip") == 1u);

    // Sequential and bounded: one request left the node in the whole run. The
    // defect issued eleven for one height, and a walk that asked two peers at
    // once would show two here.
    CHECK(log.count("[header_download] Requesting headers from") == 1u);

    // And no debt is left behind. The last thing the download task said about
    // itself is that it owes nothing -- the state the wedged node never reached.
    auto const waiting = log.last("[header_download] Waiting for events");
    REQUIRE_FALSE(waiting.empty());
    CHECK(waiting.find("pending=false") != std::string::npos);


    // And opened BY the confirmation. Bodies are owed here from the first
    // moment, so another door could open a range on its own — it does exactly
    // that when the fix is reverted — and the count alone would not tell the
    // two apart. The order does: confirmation, completion, range.
    auto const confirmed = log.first_index("confirming the tip");
    auto const completed = log.first_index("Header sync COMPLETE");
    auto const ranged = log.first_index("block_range_request");
    REQUIRE(confirmed != std::string::npos);
    REQUIRE(completed != std::string::npos);
    REQUIRE(ranged != std::string::npos);
    CHECK(confirmed < completed);
    CHECK(completed < ranged);
}

TEST_CASE("integrated: a block announced after the tip is confirmed is taken in",
          "[header_tip][integration][announcements]") {
    // The point of #706, end to end. The node confirms the tip, starts block
    // sync, and then the chain moves. Nothing polls, and until this change
    // nothing observed the eight announcements that arrived per block either —
    // so the confirmed tip stayed confirmed and the new block was never asked
    // for.
    chain_fixture fixture{"tip_announce"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 35 * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 5);
    persist_headers(fixture, trunk, 1);

    std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 2);
    connect_bodies(fixture, connected, 1);

    // The block that appears while the node is not looking. Its header is what
    // the peer will hand over when asked again.
    auto const arrival = mine_block(prev, 6, base_time + 6 * block_spacing, 0, {}, 0);
    auto const arrival_hash = arrival.hash();
    REQUIRE(fixture.organizer().index().find(arrival_hash)
        == blockchain::header_index::null_index);

    ::asio::io_context peer_ioc;
    auto peer_config = peer_settings();
    auto peer = make_peer(peer_ioc, peer_config, 9201);
    claim_height(peer, 5);

    // First the five it already has, so the tip is confirmed; then the six,
    // which is what a peer answers once the chain has moved.
    {
        network::raw_message known;
        known.payload = known_headers_payload(peer, trunk);
        REQUIRE(peer->headers_responses().try_send(std::error_code{}, std::move(known)));
    }
    auto with_arrival = trunk;
    with_arrival.push_back(arrival);
    for (int i = 0; i < 3; ++i) {
        network::raw_message extended;
        extended.payload = known_headers_payload(peer, with_arrival);
        REQUIRE(peer->headers_responses().try_send(std::error_code{}, std::move(extended)));
    }

    io_thread const peer_runner{peer_ioc};

    captured_log log;

    auto const network_config = peer_settings();
    bool confirmed = false;
    bool taken_in = false;
    {
    p2p_node network(network_config);

    auto start_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        [&network]() -> ::asio::awaitable<int> {
            auto const ec = co_await network.start();
            co_return ec.value();
        }, ::asio::use_future);
    REQUIRE(start_result.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    REQUIRE(start_result.get() == 0);

    auto orchestrator_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        sync_orchestrator(fixture.chain(), fixture.organizer(), network,
            domain::config::network::regtest,
            [](std::string const& reason) {
                spdlog::error("[test] fatal reported: {}", reason);
            }),
        ::asio::use_future);

    REQUIRE(wait_until([&]() {
        return network.peer_events().try_send(std::error_code{},
            peer_notification{peer, peer_event_type::connected});
    }, std::chrono::seconds(10)));

    // The tip is confirmed on what the node already had.
    confirmed = wait_until(
        [&]() { return log.count("confirming the tip") > 0; }, std::chrono::seconds(30));

    // Eight peers announce the same block, as they did on mainnet. The hashes
    // are registered before the doorbell rings, so the seven dropped rings
    // announce nothing the first drain will miss.
    for (int i = 0; i < 8; ++i) {
        network.announce_blocks({arrival_hash});
    }

    // And it is taken in: the announcement reopens the walk, the peer answers
    // with the longer chain, and the organizer adds the header.
    taken_in = wait_until([&]() {
        return fixture.organizer().index().find(arrival_hash)
            != blockchain::header_index::null_index;
    }, std::chrono::seconds(30));

    peer->stop();
    if (network.peer_events().try_send(std::error_code{},
            peer_notification{peer, peer_event_type::disconnected})) {
        wait_until([&]() { return log.count("Peer disconnected") > 0; },
            std::chrono::seconds(10));
    }

    network.stop();
    auto const stopped_cleanly =
        orchestrator_result.wait_for(std::chrono::seconds(120)) == std::future_status::ready;
    if (stopped_cleanly) {
        orchestrator_result.get();
    }
    CHECK(stopped_cleanly);
    }   // the node goes down here

    log.dump();

    CHECK(confirmed);
    CHECK(taken_in);

    // Eight announcements, one request. The coalescing is on the announcement,
    // not on the peer, so the seven that follow the first cost nothing.
    CHECK(log.count("Block announced and unknown; asking from height") == 1u);

    // Nothing polled, and nothing overlapped. Two requests left the node in the
    // whole run: the one that confirmed the tip, and the one the announcement
    // produced. A poll would show more; two walks at once would too.
    CHECK(log.count("[header_download] Requesting headers from") == 2u);
}

TEST_CASE("integrated: a peer arriving drives the parked request first, then clears the announcement",
          "[header_tip][integration][announcements]") {
    // The answer to "can an in-flight request stay in flight forever and strand
    // a coalesced announcement?" A request parked for want of an eligible peer
    // produces no completion event, so the coordinator still believes one is
    // outstanding — and an announcement arriving then is coalesced rather than
    // acted on.
    //
    // It is not stranded, because the parked request and the announcement are
    // waiting for the SAME event: a peer that can be asked. When it arrives the
    // parked request is driven, it asks from the tip and takes everything above
    // it — the announced block included — and the announcement is then found
    // already held and clears without a second request.
    chain_fixture fixture{"tip_parked"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 35 * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 5);
    persist_headers(fixture, trunk, 1);

    std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 2);
    connect_bodies(fixture, connected, 1);

    auto const arrival = mine_block(prev, 6, base_time + 6 * block_spacing, 0, {}, 0);
    auto const arrival_hash = arrival.hash();

    ::asio::io_context peer_ioc;
    auto peer_config = peer_settings();

    // Claims height 6 while the chain holds 5, so running out of eligible peers
    // is NOT a tip confirmation: the answer is genuinely missing and the request
    // parks. Answers empty, every time.
    auto silent = make_peer(peer_ioc, peer_config, 9301);
    claim_height(silent, 6);
    for (int i = 0; i < 4; ++i) {
        network::raw_message empty;
        empty.payload = known_headers_payload(silent, {});
        REQUIRE(silent->headers_responses().try_send(std::error_code{}, std::move(empty)));
    }

    // The peer that arrives later and can actually serve the missing header.
    auto arriving = make_peer(peer_ioc, peer_config, 9302);
    claim_height(arriving, 6);
    auto with_arrival = trunk;
    with_arrival.push_back(arrival);
    {
        network::raw_message extended;
        extended.payload = known_headers_payload(arriving, with_arrival);
        REQUIRE(arriving->headers_responses().try_send(std::error_code{}, std::move(extended)));
    }
    for (int i = 0; i < 4; ++i) {
        network::raw_message empty;
        empty.payload = known_headers_payload(arriving, {});
        REQUIRE(arriving->headers_responses().try_send(std::error_code{}, std::move(empty)));
    }

    io_thread const peer_runner{peer_ioc};

    captured_log log;

    auto const network_config = peer_settings();
    bool parked = false;
    bool coalesced = false;
    bool taken_in = false;
    {
    p2p_node network(network_config);

    auto start_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        [&network]() -> ::asio::awaitable<int> {
            auto const ec = co_await network.start();
            co_return ec.value();
        }, ::asio::use_future);
    REQUIRE(start_result.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    REQUIRE(start_result.get() == 0);

    auto orchestrator_result = ::asio::co_spawn(network.thread_pool().get_executor(),
        sync_orchestrator(fixture.chain(), fixture.organizer(), network,
            domain::config::network::regtest,
            [](std::string const& reason) {
                spdlog::error("[test] fatal reported: {}", reason);
            }),
        ::asio::use_future);

    REQUIRE(wait_until([&]() {
        return network.peer_events().try_send(std::error_code{},
            peer_notification{silent, peer_event_type::connected});
    }, std::chrono::seconds(10)));

    // The request parks: nobody left to ask, and the chain is short of what the
    // peer claims.
    parked = wait_until(
        [&]() { return log.count("): waiting") > 0; }, std::chrono::seconds(30));

    // Two blocks are announced while that request is still outstanding, in two
    // separate announcements so the order is this test's and not a container's.
    // Only the second is one a peer here will ever serve.
    //
    // Keeping a single hash would keep the LAST, and the walk brings that one
    // in — so the debt would read as settled and the first block, which nobody
    // served, would never be asked about again.
    auto const never_served = kth::domain::chain::hash(
        mine_block(arrival_hash, 7, base_time + 7 * block_spacing, 0, {}, 0).header());

    network.announce_blocks({never_served});
    coalesced = wait_until(
        [&]() { return log.count("coalescing until it settles") > 0; },
        std::chrono::seconds(30));
    REQUIRE(coalesced);

    network.announce_blocks({arrival_hash});
    REQUIRE(wait_until(
        [&]() { return log.count("coalescing until it settles") > 1; },
        std::chrono::seconds(30)));

    // And now a peer that can serve it arrives.
    REQUIRE(wait_until([&]() {
        return network.peer_events().try_send(std::error_code{},
            peer_notification{arriving, peer_event_type::connected});
    }, std::chrono::seconds(10)));

    taken_in = wait_until([&]() {
        return fixture.organizer().index().find(arrival_hash)
            != blockchain::header_index::null_index;
    }, std::chrono::seconds(30));

    // Give the coordinator its chance to consult the coalesced announcement.
    wait_until([&]() { return log.count("no follow-up needed") > 0; },
        std::chrono::seconds(30));

    silent->stop();
    arriving->stop();
    network.stop();
    auto const stopped_cleanly =
        orchestrator_result.wait_for(std::chrono::seconds(120)) == std::future_status::ready;
    if (stopped_cleanly) {
        orchestrator_result.get();
    }
    CHECK(stopped_cleanly);
    }

    log.dump();

    CHECK(parked);
    CHECK(coalesced);
    CHECK(taken_in);

    // The order is the point: the arriving peer drove the parked request first,
    // and only then was the announcement consulted.
    //
    // And what it found was that one of the two announced blocks had arrived
    // and the other had not, so it asked once — for the one still missing.
    // Keeping a single hash would have answered "all known" here and dropped
    // the second block on the floor.
    CHECK(log.count("Announced block still unknown after the walk settled") == 1u);
    CHECK(log.count("Announced block already in the index; no follow-up needed") == 0u);
}
