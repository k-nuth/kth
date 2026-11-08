// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/blockchain/local_block_admission.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/orchestrator.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// A body this node holds itself is never asked of peers
// =============================================================================
//
// The real orchestrator over a real chain, with the endpoint bound and no
// peers at all, so every body range the coordinator opens is read off the log
// line that opens it. The defect: a locally submitted body above the body
// frontier was excluded from the range only while it was on its way to
// storage. Once stored, the next range the coordinator opened ran up to the
// header tip and asked peers for a block only this node has — a chunk that
// never completes, and bodies below it in that chunk never arrive.
//
// Holding the range open until the submissions are in uses the one hold the
// coordinator has: the UTXO admission. Bodies stored past what is built keep
// it shut; a stale header tip keeps the builder from building. The second
// submission, with a recent timestamp, is what lets the builder move and the
// admission open — and the range it opens is the observation.

namespace {

network::settings node_settings(std::string const& tag) {
    network::settings settings;
    settings.identifier = 0xdab5bffa;           // regtest
    settings.protocol_maximum = 70015;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;          // nothing dials out
    settings.threads = 1;
    settings.hosts_file = std::filesystem::temp_directory_path() / ("kth_" + tag + "_hosts_nonexistent.dat");
    settings.banlist_file = std::filesystem::temp_directory_path() / ("kth_" + tag + "_banlist_nonexistent.dat");
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

// Swap the default logger for one that keeps what was written.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(262144))
        , installed(std::make_shared<spdlog::logger>("captured_range", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    void dump(std::string const& name) const {
        auto const path = std::filesystem::temp_directory_path() / ("kth_" + name + "_dump.log");
        auto* out = std::fopen(path.c_str(), "wb");
        if (out != nullptr) {
            for (auto const& line : sink->last_formatted()) {
                (void)std::fwrite(line.data(), 1, line.size(), out);
            }
            std::fclose(out);
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
};

// The node and its orchestrator with the endpoint bound, and a context of
// the submitter's own, run on its own thread for the whole case: the replies
// the coordinator sends are dispatched through it, so it has to outlive them.
struct node_run {
    network::settings const config;
    p2p_node network;
    std::shared_ptr<blockchain::block_submission_channel> endpoint;
    std::future<void> orchestrator;
    ::asio::io_context submitter;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard{::asio::make_work_guard(submitter)};
    std::thread submitter_thread;
    bool stopped{false};

    node_run(chain_fixture& fixture, std::shared_ptr<blockchain::block_submission_channel> submissions,
             std::string const& tag)
        : config(node_settings(tag))
        , network(config)
        , endpoint(std::move(submissions))
        , submitter_thread([this] { submitter.run(); }) {
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
                node_header_walk_timing, endpoint.get()),
            ::asio::use_future);
    }

    ~node_run() {
        if ( ! stopped) {
            (void)stop(std::chrono::seconds(120));
        }
        guard.reset();
        submitter.stop();
        if (submitter_thread.joinable()) {
            submitter_thread.join();
        }
    }

    node_run(node_run const&) = delete;
    node_run& operator=(node_run const&) = delete;

    // Hand a block to the chain's endpoint; the answer, if any, arrives on
    // the future, through the submitter's context.
    std::future<blockchain::block_submission_result> submit(blockchain::block_chain& chain, domain::chain::block const& blk) {
        return ::asio::co_spawn(submitter,
            chain.submit_block(std::make_shared<domain::message::block const>(blk)),
            ::asio::use_future);
    }

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

constexpr auto quick = std::chrono::milliseconds(15000);

// Put one body on disk through the real storage task, without connecting it.
void store_body(chain_fixture& fixture, domain::chain::block const& blk, uint32_t height) {
    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 4);
    chunk_validated_channel output(ctx.get_executor(), 4);
    std::atomic<uint32_t> contiguous{height};
    auto task = ::asio::co_spawn(ctx,
        block_storage_task(fixture.chain(), input, output, height, fixture.organizer(), &contiguous),
        ::asio::use_future);
    REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = height,
        .chunk_id = 0,
        .blocks = {to_light(blk)},
        .source_peer = nullptr,
        .generation = fixture.chain().headers().generation()}));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ctx.run_for(std::chrono::seconds(10));
    REQUIRE(task.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    task.get();
    auto const& index = fixture.chain().headers();
    REQUIRE(index.has_block_data(index.find(blk.hash())));
}

} // namespace

TEST_CASE("integrated: a local body stored above the body frontier is not asked of peers",
          "[local_admission][integration][range]") {
    // First, so it is installed before any thread that logs exists and
    // removed after all of them are gone.
    captured_log log;

    // Recency limit of one hour, and a chain three hours old: the header tip
    // is stale, so the builder waits for a full batch it will never have, and
    // nothing it has not built is admitted to a range.
    chain_fixture fixture("local_range", 1u);
    REQUIRE(fixture.created());
    auto submissions = std::make_shared<blockchain::block_submission_channel>(4, 1);
    REQUIRE(fixture.start(submissions));
    auto& chain = fixture.chain();
    auto const& index = chain.headers();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 3u * 3600u;

    // Headers 1..5, body 1 stored and not connected. The coordinator starts
    // with bodies at 1, headers at 5, nothing built: the range [2..5] is owed
    // and held by the admission, since the set does not describe block 1 yet.
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 5u);
    persist_headers(fixture, trunk, 1);
    store_body(fixture, trunk.front(), 1);

    // Two blocks on the tip: the first as old as the chain, so the hold
    // stays; the second recent, which is what releases the builder.
    auto const six = mine_block(trunk.back().hash(), 6, base_time + 6 * block_spacing, 0, {}, 0);
    auto const seven = mine_block(six.hash(), 7, uint32_t(zulu_time()), 0, {}, 0);

    bool held = false;
    bool six_stored = false;
    bool six_unanswered = false;
    bool opened = false;
    bool stopped_cleanly = false;
    std::optional<code> six_outcome;
    std::optional<code> seven_outcome;
    {
        node_run run(fixture, submissions, "local_range");

        // The header is taken; the range its admission would open is held,
        // since nothing below it is built. The body is stored all the same.
        auto six_answer = run.submit(chain, six);
        held = wait_until([&] { return log.count("SLOW block sync held at 2") > 0; }, quick);
        six_stored = wait_until([&] {
            auto const idx = index.find(six.hash());
            return idx != blockchain::header_index::null_index && index.has_block_data(idx);
        }, quick);
        REQUIRE(held);
        REQUIRE(six_stored);
        // Stored, not connected: block 1 is still unbuilt below it.
        six_unanswered = six_answer.wait_for(std::chrono::seconds(0)) == std::future_status::timeout;

        // A recent tip: the builder builds block 1, the admission opens, and
        // the coordinator opens the range it believes is owed.
        auto seven_answer = run.submit(chain, seven);
        opened = wait_until([&] { return log.count("block_range_request 2-") > 0; }, quick);

        stopped_cleanly = run.stop(std::chrono::seconds(120));

        // Read while the submitter's context still runs: the coordinator's
        // answers are dispatched through it.
        if (six_answer.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            six_outcome = six_answer.get().error;
        }
        if (seven_answer.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            seven_outcome = seven_answer.get().error;
        }
    }
    log.dump("local_range");

    CHECK(stopped_cleanly);
    CHECK(six_unanswered);
    REQUIRE(opened);

    // The range stops where the bodies this node holds begin. With the suffix
    // not excluded it ran to 6 or to 7, whichever was stored by then — both
    // blocks nobody else has.
    CHECK(log.count("block_range_request 2-5 sent") == 1u);
    CHECK(log.count("block_range_request 2-6") == 0u);
    CHECK(log.count("block_range_request 2-7") == 0u);

    // Neither submission could be connected on a node with no peers, and both
    // were answered when the coordinator ended rather than left waiting.
    REQUIRE(six_outcome.has_value());
    REQUIRE(seven_outcome.has_value());
    CHECK(*six_outcome == error::service_stopped);
    CHECK(*seven_outcome == error::service_stopped);
}

TEST_CASE("local admission: the emitted body request excludes a stored active suffix",
          "[local_admission][range_message]") {
    chain_fixture fixture("stored_suffix_request");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    std::vector<domain::chain::block> blocks;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= 7; ++height) {
        blocks.push_back(mine_block(previous, height,
            uint32_t(zulu_time()) - 1200 + height * 120, height, {}, 0));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == blocks.size());
    persist_headers(fixture, blocks, 1);
    store_body(fixture, blocks[5], 6);
    store_body(fixture, blocks[6], 7);
    auto config = node_settings("stored_suffix_request");
    p2p_node network(config);
    ::asio::io_context io;
    block_download_input_channel requests(4);
    body_range_deps const deps{fixture.chain(), network, requests, 0};
    body_range_log_memory memory;
    auto const end = trim_held_suffix(fixture.chain().headers(), 0, 7,
        [](uint32_t) { return false; });
    headers_validated const advanced{.height = 7, .count = 7, .result = {}, .source_peer = nullptr};
    auto task = ::asio::co_spawn(io, on_headers_advanced(deps, memory, advanced,
        0, 0, std::nullopt, end), ::asio::use_future);
    io.run();
    CHECK(task.get().headers_synced_to == 7u);
    std::error_code ec;
    auto command = requests.try_receive(ec);
    REQUIRE(command);
    auto const* range = std::get_if<block_range_request>(&*command);
    REQUIRE(range != nullptr);
    CHECK(range->start_height == 1u);
    CHECK(range->end_height == 5u);
}
