// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node;
using namespace kth::node::sync;

// =============================================================================
// A restarted node resumes the body download above the bodies it already holds
// =============================================================================
//
// A mainnet node stopped partway through its initial sync held bodies stored and
// contiguous to 859056 while its UTXO set, and with it the connected tip, stood at
// 364200. On restart it scanned its own block files and restored 859728 positions,
// then one second later asked the network for [364201..969087]: the coordinator
// began from the CONNECTED tip, and nothing it consulted said what the store held.
// Every chunk that came back was written again to new block files and the index
// re-pointed at the copy — 64 GB in thirteen minutes, for blocks that were already
// there — and the run could not reach the tip on the disk it had.
//
// The second face of the same defect shows without any peer: the counter the UTXO
// build follows starts at the connected tip too, so bodies on disk above it are
// invisible to the build until a re-downloaded chunk happens to be stored. A node
// that comes up with no peer serving blocks validates nothing it already has.
//
// The cases below stage the store as the mainnet node had it — headers ahead,
// bodies stored above the connected tip, closed and reopened through the real
// flat-file scan — and run the REAL orchestrator over it. What they read is what
// the node does: the heights it requests, the bytes it writes, and how far the
// UTXO set gets on the bodies it already holds.

namespace {

using peer_ptr = network::peer_session::ptr;

// Swap the default logger for one that keeps what was written.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
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

    [[nodiscard]] size_t count(std::string_view needle) const {
        size_t n = 0;
        for (auto const& line : sink->last_formatted()) {
            if (line.find(needle) != std::string::npos) ++n;
        }
        return n;
    }

    // The first line holding `needle`, or nothing.
    [[nodiscard]] std::optional<std::string> first_line(std::string_view needle) const {
        for (auto const& line : sink->last_formatted()) {
            if (line.find(needle) != std::string::npos) return line;
        }
        return std::nullopt;
    }
};

// A network that connects to nothing and writes nothing shared.
network::settings node_settings(std::string const& tag) {
    network::settings settings;
    settings.identifier = 0xdab5bffa;           // regtest
    settings.protocol_maximum = 70015;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_" + tag + "_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_" + tag + "_banlist_nonexistent.dat";
    auto const peers = std::filesystem::temp_directory_path() / ("kth_" + tag + "_peers.dat");
    std::filesystem::remove(peers);
    settings.peers_file = peers;
    return settings;
}

bool wait_until(std::function<bool()> const& done, std::chrono::milliseconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// A contiguous chain whose timestamps end near the present, so the node is not
// judged stale and the builder drains a remainder shorter than a batch.
std::vector<domain::chain::block> make_chain(domain::chain::block const& genesis, uint32_t len) {
    std::vector<domain::chain::block> blocks;
    auto prev = genesis.hash();
    auto const base = uint32_t(zulu_time()) - (len + 2) * block_spacing;
    for (uint32_t i = 0; i < len; ++i) {
        blocks.push_back(mine_block(prev, i + 1, base + i * block_spacing, 0, {}, 0));
        prev = blocks.back().hash();
    }
    return blocks;
}

// Where the index says a block's bytes are.
struct block_location {
    int16_t file;
    uint32_t pos;
    friend bool operator==(block_location const&, block_location const&) = default;
};

std::vector<block_location> locations(blockchain::block_chain& chain, uint32_t from, uint32_t to) {
    std::vector<block_location> out;
    auto const& index = chain.headers();
    for (uint32_t h = from; h <= to; ++h) {
        auto const idx = index.active_at(static_cast<int32_t>(h));
        REQUIRE(idx != blockchain::header_index::null_index);
        REQUIRE(index.has_block_data(idx));
        out.push_back({index.get_file_number(idx), index.get_data_pos(idx)});
    }
    return out;
}

// Store bodies WITHOUT connecting them: the storage task on its own, which is the
// state a download that outran the UTXO build leaves behind. The task marks the
// blocks have_data and publishes nothing to the connected marker (#653).
void store_only(chain_fixture& fixture, std::vector<domain::chain::block> const& blocks,
                uint32_t start_height) {
    auto& chain = fixture.chain();
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& blk : blocks) {
        light.push_back(to_light(blk));
    }

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 256);
    std::atomic<uint32_t> contiguous{start_height};

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, start_height, fixture.organizer(), &contiguous),
        ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = start_height,
        .chunk_id = 0,
        .blocks = std::move(light),
        .source_peer = nullptr,
        .generation = chain.headers().generation()
    }));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ctx.run_for(std::chrono::seconds(60));
    while (output.try_receive([](std::error_code, chunk_validated) {})) {}

    REQUIRE(contiguous.load() == start_height + uint32_t(blocks.size()));
}

// The store as the mainnet node had it: headers to `headers_to`, bodies connected
// to `connected_to`, bodies stored but not connected up to `stored_to`. Then
// closed and reopened, which is what rebuilds the index from the block files.
struct staged_store {
    std::vector<domain::chain::block> trunk;
    std::vector<block_location> stored_before;   // where (connected_to, stored_to] sat
};

staged_store stage(chain_fixture& fixture, uint32_t headers_to, uint32_t connected_to,
                   uint32_t stored_to) {
    REQUIRE(connected_to >= 1);
    REQUIRE(stored_to > connected_to);
    REQUIRE(headers_to >= stored_to);

    auto trunk = make_chain(domain::chain::block::genesis_regtest(), headers_to);
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == headers_to);
    // Persisted the way the node persists a header sync: through the barrier that
    // also records how far the table reaches, so the connect below extends it
    // rather than re-pushing from 1 and leaving the marker at its batch end.
    REQUIRE(ensure_headers_persisted(fixture.chain(), fixture.chain().headers(), headers_to));

    std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + connected_to);
    connect_bodies(fixture, connected, 1);

    std::vector<domain::chain::block> const stored(
        trunk.begin() + connected_to, trunk.begin() + stored_to);
    store_only(fixture, stored, connected_to + 1);

    auto& chain = fixture.chain();
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    REQUIRE(heights->block == connected_to);
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    REQUIRE(*built == connected_to);
    auto stored_before = locations(chain, connected_to + 1, stored_to);

    // The process restart: nothing in memory survives, and the block positions
    // come back from the files themselves.
    REQUIRE(fixture.restart());
    auto& reopened = fixture.chain();
    auto const after = reopened.get_last_heights();
    REQUIRE(after);
    REQUIRE(after->header == headers_to);
    REQUIRE(after->block == connected_to);
    REQUIRE(reopened.get_utxo_built_height().value() == connected_to);
    REQUIRE(locations(reopened, connected_to + 1, stored_to) == stored_before);

    return {std::move(trunk), std::move(stored_before)};
}

// The node and its orchestrator, started and torn down in the order the
// orchestrator can actually finish in.
struct node_run {
    network::settings const config;     // p2p_node keeps a reference
    p2p_node network;
    std::future<void> orchestrator;
    bool stopped{false};
    std::vector<peer_ptr> connected;

    // `peers` are announced to the sync layer BEFORE the orchestrator starts, so
    // the notification is waiting in the channel when its bridge first reads
    // it. Announced after the start, it races the bridge's 500 ms check timer
    // (the two are composed with `||`), and a notification that lands as the
    // timer fires is dropped: the peer is connected and never used. That is a
    // property of the node, not of these cases, and they do not depend on it.
    node_run(chain_fixture& fixture, std::string const& tag, std::vector<peer_ptr> const& peers = {})
        : config(node_settings(tag))
        , network(config) {
        auto started = ::asio::co_spawn(network.thread_pool().get_executor(),
            [this]() -> ::asio::awaitable<int> {
                auto const ec = co_await network.start();
                co_return ec.value();
            }, ::asio::use_future);
        REQUIRE(started.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        REQUIRE(started.get() == 0);

        for (auto const& peer : peers) {
            connect(peer);
        }

        orchestrator = ::asio::co_spawn(network.thread_pool().get_executor(),
            sync_orchestrator(fixture.chain(), fixture.organizer(), network,
                domain::config::network::regtest,
                [](std::string const& reason) {
                    spdlog::error("[test] fatal reported: {}", reason);
                },
                node_header_walk_timing),
            ::asio::use_future);
    }

    ~node_run() {
        if ( ! stopped) {
            (void)stop(std::chrono::seconds(120));
        }
    }

    void connect(peer_ptr const& peer) {
        connected.push_back(peer);
        REQUIRE(wait_until([&] {
            return network.peer_events().try_send(std::error_code{},
                peer_notification{peer, peer_event_type::connected});
        }, std::chrono::seconds(10)));
    }

    // The peers first, so a download waiting on them completes, then the node.
    // Answers whether the orchestrator finished within `limit`.
    bool stop(std::chrono::seconds limit) {
        stopped = true;
        for (auto const& peer : connected) {
            peer->stop();
        }
        network.stop();
        if (orchestrator.wait_for(limit) != std::future_status::ready) {
            return false;
        }
        orchestrator.get();
        return true;
    }
};

// A peer session on one end of a loopback connection, running on a thread of
// its own, as every session does on a node. What the node sends it goes out on
// the socket and is left unread; what it "sends" back is put on the channel the
// block download reads, which is where the read loop would put a `block` that
// arrived on the wire.
struct quiet_peer {
    network::settings const config;
    ::asio::io_context ioc;
    ::asio::io_context client_ioc;          // owns the test's end; never run
    ::asio::ip::tcp::socket client{client_ioc};
    peer_ptr session;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard{::asio::make_work_guard(ioc)};
    std::thread runner;

    quiet_peer(std::string const& tag, uint64_t nonce, uint32_t claimed_height)
        : config(node_settings(tag)) {
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
        ::asio::co_spawn(ioc, [peer = session]() -> ::asio::awaitable<void> {
            (void)co_await peer->run();
        }, ::asio::detached);
    }

    ~quiet_peer() {
        session->stop();
        std::error_code ignored;
        client.close(ignored);
        guard.reset();
        ioc.stop();
        if (runner.joinable()) runner.join();
    }

    quiet_peer(quiet_peer const&) = delete;
    quiet_peer& operator=(quiet_peer const&) = delete;

    // Serve a block as the wire would deliver it.
    bool serve(domain::chain::block const& blk) const {
        data_chunk payload(blk.serialized_size());
        byte_writer writer(payload);
        REQUIRE(blk.to_data(writer));
        network::raw_message raw;
        raw.heading = domain::message::heading(0xdab5bffa, domain::message::block::command,
            uint32_t(payload.size()), 0);
        raw.payload = std::move(payload);
        return session->block_responses().try_send(std::move(raw));
    }
};

// The heights of the one getdata the download sent, read from the line the
// protocol writes for it: "Requesting N blocks in batch from [peer] (a-b)".
std::optional<std::pair<uint32_t, uint32_t>> requested_range(captured_log const& log) {
    auto const line = log.first_line("blocks in batch from");
    if ( ! line) return std::nullopt;
    auto const open = line->rfind('(');
    auto const dash = line->find('-', open);
    auto const close = line->find(')', dash);
    if (open == std::string::npos || dash == std::string::npos || close == std::string::npos) {
        return std::nullopt;
    }
    return std::pair{
        uint32_t(std::stoul(line->substr(open + 1, dash - open - 1))),
        uint32_t(std::stoul(line->substr(dash + 1, close - dash - 1)))};
}

constexpr auto quick = std::chrono::milliseconds(15000);

} // namespace

TEST_CASE("resume: bodies stored above the connected tip are validated from disk with no peer",
          "[node][sync][resume_bodies]") {
    // First, so it is installed before any thread that logs exists and removed
    // after all of them are gone.
    captured_log log;

    // Headers level with the stored bodies: nothing is owed to the network at
    // all. The only work is on disk, and nobody is there to download it twice.
    chain_fixture fixture{"resume_bodies_no_peer"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const staged = stage(fixture, 6, 3, 6);
    auto& chain = fixture.chain();

    bool built_to_stored = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "resume_bodies_no_peer");

        // THE assertion. Under the defect the build follows a counter that starts
        // at the connected tip and nothing moves it, so the set stays at 3.
        built_to_stored = wait_until([&] {
            auto const built = chain.get_utxo_built_height();
            return built && *built >= 6u;
        }, quick);

        stopped_cleanly = run.stop(std::chrono::seconds(120));
    }
    log.dump("resume_bodies_no_peer");

    CHECK(stopped_cleanly);
    CHECK(built_to_stored);
    CHECK(log.count("Initial state: blocks=6, headers=6") == 1u);
    CHECK(log.count("block_range_request") == 0u);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 6u);
    // And the bytes were not touched: the three bodies sit where they were stored.
    CHECK(locations(chain, 4, 6) == staged.stored_before);
    CHECK(log.count("fatal reported") == 0u);
}

TEST_CASE("resume: the body range opens above the stored bodies, once the set reaches them",
          "[node][sync][resume_bodies]") {
    captured_log log;

    // Headers two past the stored bodies. What is owed to the network is 7-8;
    // 4-6 are on disk and owed to nobody.
    chain_fixture fixture{"resume_bodies_range"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const staged = stage(fixture, 8, 3, 6);
    auto& chain = fixture.chain();

    bool built_to_stored = false;
    bool opened_above = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "resume_bodies_range");

        built_to_stored = wait_until([&] {
            auto const built = chain.get_utxo_built_height();
            return built && *built >= 6u;
        }, quick);

        // The range the coordinator asks for starts where the store ends. It is
        // released by the builder reaching 6, through the same admission every
        // post-checkpoint range goes through.
        opened_above = wait_until([&] {
            return log.count("block_range_request 7-8 sent") > 0;
        }, quick);

        stopped_cleanly = run.stop(std::chrono::seconds(120));
    }
    log.dump("resume_bodies_range");

    CHECK(stopped_cleanly);
    CHECK(built_to_stored);
    CHECK(opened_above);
    CHECK(log.count("Initial state: blocks=6, headers=8") == 1u);
    // Never the range from the connected tip, which is the one the mainnet node
    // opened: [364201..969087] over bodies stored to 859056.
    CHECK(log.count("block_range_request 4-8") == 0u);
    CHECK(log.count("block_range_request") == 1u);
    CHECK(locations(chain, 4, 6) == staged.stored_before);
    CHECK(log.count("fatal reported") == 0u);
}

TEST_CASE("resume: a peer is asked only for the bodies the store lacks, and nothing is stored twice",
          "[node][sync][resume_bodies]") {
    captured_log log;

    chain_fixture fixture{"resume_bodies_peer"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const staged = stage(fixture, 8, 3, 6);
    auto& chain = fixture.chain();

    quiet_peer peer("resume_bodies_peer_session", 9301, 8);

    std::optional<std::pair<uint32_t, uint32_t>> asked;
    bool served = false;
    bool built_to_tip = false;
    bool stopped_cleanly = false;
    {
        node_run run(fixture, "resume_bodies_peer", {peer.session});

        // What the download asks this peer for. Under the defect it is 4-8:
        // three bodies the store holds and two it does not.
        if (wait_until([&] { return requested_range(log).has_value(); }, quick)) {
            asked = requested_range(log);
        }

        if (asked && asked->first >= 1 && asked->second <= 8) {
            // Serve exactly what was asked, the way the mainnet peers did.
            served = true;
            for (uint32_t h = asked->first; h <= asked->second; ++h) {
                served = served && peer.serve(staged.trunk[h - 1]);
            }

            built_to_tip = wait_until([&] {
                auto const built = chain.get_utxo_built_height();
                return built && *built >= 8u;
            }, quick);
        }

        stopped_cleanly = run.stop(std::chrono::seconds(120));
    }
    // Written before any assertion, so a run that asked for nothing leaves the
    // log that says why.
    log.dump("resume_bodies_peer");

    // Nothing of the node outlives its stop holding this session. Under GCC
    // 16.1 at -O2/-O3, a loop-body `auto [ec, msg] = co_await ...` kept each
    // received message alive past its iteration (see block_tasks.cpp,
    // fast_validation_task and block_storage_task), so every chunk a peer
    // served left one reference to the peer per task: this read 3 here.
    CHECK(peer.session.use_count() == 1);

    CHECK(stopped_cleanly);
    CHECK(served);
    CHECK(built_to_tip);
    REQUIRE(asked);
    CHECK(asked->first == 7u);
    CHECK(asked->second == 8u);

    // The three bodies the store already held were not written again: their
    // bytes are where they were before the restart. Under the defect every one
    // of them moved to the new copy, as 364201 onwards did on mainnet.
    CHECK(locations(chain, 4, 6) == staged.stored_before);
    // And the two that were owed are now held too, above them.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 8u);
    CHECK(log.count("fatal reported") == 0u);
}
