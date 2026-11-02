// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <asio/experimental/channel_select.hpp>

#include <kth/domain/message/block.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/messages.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/detail/body_range.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/chunk_coordinator.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include "../../blockchain/test/regtest_miner.hpp"
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "sync_harness.hpp"

namespace kth::test {
// A chain whose timestamps end near the present, as body_range_reopen builds one.
inline std::vector<domain::chain::block> make_chain_for_select(domain::chain::block const& genesis, uint32_t len) {
    std::vector<domain::chain::block> out;
    auto prev = genesis.hash();
    auto const base = uint32_t(zulu_time()) - (len + 2) * block_spacing;
    for (uint32_t i = 0; i < len; ++i) {
        out.push_back(mine_block(prev, i + 1, base + i * block_spacing, 0, {}, 0));
        prev = out.back().hash();
    }
    return out;
}
} // namespace kth::test

using namespace kth;
using namespace kth::node::sync;
using namespace std::chrono_literals;

// =============================================================================
// Channel selection in the block sync (fork of Asio: select_channel/selector)
// =============================================================================
//
// These drive the REAL coroutines over REAL select_channels: the download
// supervisor's one selector over its input, its workers' output and its tick;
// a real block_download_task over a real peer_session on a loopback socket,
// suspended on a full fast-validation channel; the real fast_validation_task's
// receive loop; and the coordinator's range request. What they pin down is
// ownership and progress at the boundaries the readiness review named:
//
//   * several ready sources: nothing lost, each source in order, control before data;
//   * a full consumer channel, then capacity released by a receive: progress is
//     event-driven, and the worker holds exactly one chunk while it waits;
//   * a critical report (task_ended) under load: waited out, never dropped;
//   * the peer's reader keeps delivering while its download worker is suspended;
//   * cancellation (coordinator stop), closure (shutdown) and a stopped peer while
//     a chunk is held: who owns the chunk afterwards, and that every task ends.
//
// No sleep is a verdict. The contexts are driven until a condition holds; the
// budgets below only turn a hang into a named failure.

namespace {

struct watchdog {
    std::atomic<bool> done{false};
    std::thread barker;

    watchdog(std::chrono::seconds budget, char const* what)
        : barker([this, budget, what] {
              auto const deadline = std::chrono::steady_clock::now() + budget;
              while ( ! done.load()) {
                  if (std::chrono::steady_clock::now() > deadline) {
                      std::fprintf(stderr, "\n[watchdog] %s did not finish: treating it "
                          "as a sync defect\n", what);
                      std::abort();
                  }
                  std::this_thread::sleep_for(10ms);
              }
          }) {}

    ~watchdog() {
        done.store(true);
        barker.join();
    }
};

// Drive `ctx` until `pred` holds. Returns false only past the budget, which the
// caller turns into a named failure: the budget is never the verdict.
template <typename Pred>
bool run_until(::asio::io_context& ctx, Pred pred, std::chrono::seconds budget = 30s) {
    auto const deadline = std::chrono::steady_clock::now() + budget;
    ctx.restart();
    while ( ! pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        ctx.run_one_for(20ms);
    }
    return true;
}

// Drive `ctx` without waiting for future events. Already-ready timers may run,
// along with handlers that are queued or queued by one that runs.
// Returns false as soon as the context runs dry with `pred` still false.
template <typename Pred>
bool drain_until(::asio::io_context& ctx, Pred pred) {
    ctx.restart();
    while ( ! pred()) {
        if (ctx.poll() == 0) return pred();
    }
    return true;
}

// Spawn a coroutine whose end and exception are observable, unlike detached.
struct tracked {
    std::atomic<bool> done{false};
    std::exception_ptr error;

    template <typename Awaitable>
    void spawn(::asio::io_context& ctx, Awaitable&& coro) {
        ::asio::co_spawn(ctx, std::forward<Awaitable>(coro), [this](std::exception_ptr e) {
            error = e;
            done.store(true);
        });
    }

    void check() const {
        if (error) std::rethrow_exception(error);
    }
};

network::peer_session::ptr make_idle_peer(::asio::io_context& ctx, uint64_t nonce) {
    network::settings settings(domain::config::network::regtest);
    ::asio::ip::tcp::socket sock(ctx);
    auto peer = std::make_shared<network::peer_session>(std::move(sock), settings);
    peer->set_nonce(nonce);
    return peer;
}

// -----------------------------------------------------------------------------
// The supervisor, as in download_supervisor.cpp, with one more seam setting:
// the launch may run the REAL worker against a coordinator that is already
// stopped, so the worker's only act is its task_ended report — the production
// suspended send, exercised under a full channel.
// -----------------------------------------------------------------------------

struct launch_record {
    uint64_t nonce{0};
    uint64_t task_id{0};
    uint64_t epoch{0};
    std::shared_ptr<chunk_coordinator> coordinator;
    network::peer_session::ptr peer;
};

struct recording_launcher {
    std::vector<launch_record> launches;
    block_download_task_output_channel* output{nullptr};

    download_worker_launcher make() {
        return [this](network::peer_session::ptr peer,
                      std::shared_ptr<chunk_coordinator> coordinator,
                      uint64_t task_id, uint64_t epoch,
                      block_download_task_output_channel& out) {
            output = &out;
            launches.push_back(launch_record{peer->nonce(), task_id, epoch, coordinator, peer});
        };
    }

    void end(size_t index) {
        REQUIRE(index < launches.size());
        REQUIRE(output != nullptr);
        auto& l = launches[index];
        REQUIRE(output->try_send(block_download_task_output{
            download_task_ended{l.nonce, l.task_id, l.epoch}}));
        l.peer.reset();
    }

    [[nodiscard]] size_t count_for_epoch(uint64_t epoch) const {
        return size_t(std::ranges::count_if(launches,
            [epoch](auto const& l) { return l.epoch == epoch; }));
    }
};

struct supervisor_fixture {
    ::asio::io_context ctx;
    ::asio::io_context report_ctx;
    std::vector<std::shared_ptr<tracked>> report_workers;
    block_download_input_channel input{64};
    block_download_channel output{ctx.get_executor(), 1024};
    test::chain_fixture chain{"select_sync_supervisor"};
    std::atomic<uint32_t> active_peers{0};
    recording_launcher launcher;
    tracked supervisor;
    bool running{false};

    supervisor_fixture() {
        REQUIRE(chain.created());
        REQUIRE(chain.start());
    }

    // The REAL worker for a recorded launch, started against its coordinator
    // after that coordinator has been stopped, so its only act is the task_ended
    // report — the production suspended send — with the identity the seam was
    // handed. Spawned by the test, not by the seam, so the test chooses the
    // moment: a report sent while the supervisor cannot be draining is a report
    // that finds the channel in the state the test put it in.
    std::shared_ptr<tracked> start_real_worker(size_t index) {
        REQUIRE(index < launcher.launches.size());
        auto& l = launcher.launches[index];
        l.coordinator->stop();
        auto w = std::make_shared<tracked>();
        w->spawn(report_ctx, block_download_task(l.peer, l.coordinator, active_peers, *launcher.output,
            l.task_id, l.epoch, nullptr));
        l.peer.reset();
        report_workers.push_back(w);
        return w;
    }

    void start() {
        supervisor.spawn(ctx,
            block_download_supervisor(input, output, chain.organizer(), nullptr, launcher.make()));
        running = true;
        pump();
    }

    void pump() {
        ctx.restart();
        ctx.poll();
    }

    void send(block_download_input msg) {
        REQUIRE(input.try_send(std::move(msg)));
        pump();
    }

    // Fill the workers' output channel with performance reports, which the
    // supervisor forwards best-effort. Returns the admitted count: the buffer
    // capacity plus an extra item if the output receive is still eligible.
    size_t flood_task_output() {
        REQUIRE(launcher.output != nullptr);
        size_t sent = 0;
        while (launcher.output->try_send(block_download_task_output{
                peer_performance{.peer_nonce = 1, .blocks_downloaded = 1, .download_time_ms = 1}})) {
            ++sent;
        }
        return sent;
    }

    size_t forwarded_performance() {
        size_t count = 0;
        while (output.try_receive([&](std::error_code, block_supervisor_output msg) {
            if (std::holds_alternative<peer_performance>(msg)) ++count;
        })) {}
        return count;
    }

    void drain_and_check() {
        if ( ! running) return;
        running = false;

        (void)input.try_send(block_download_input{stop_request{}});
        pump();
        ctx.restart();
        ctx.run_for(5s);

        bool leftover = false;
        std::error_code ec;
        while (input.try_receive(ec)) { leftover = true; }
        CHECK_FALSE(leftover);
        CHECK(ctx.stopped());
        CHECK(supervisor.done.load());
        supervisor.check();
        // Closing task_output wakes a suspended report on its own executor.
        report_ctx.restart();
        report_ctx.run_for(5s);
        for (auto const& worker : report_workers) {
            CHECK(worker->done.load());
            worker->check();
        }
        CHECK(report_ctx.stopped());
    }

    ~supervisor_fixture() {
        drain_and_check();
    }
};

} // namespace

// =============================================================================
// Supervisor: several ready sources
// =============================================================================

TEST_CASE("select: two sources ready at once lose nothing and keep each source's order",
          "[node][select_sync][supervisor]") {
    // Two reports queued in the workers' output and a new range queued in the
    // input BEFORE the supervisor runs. With the || operator a losing receive
    // could consume a message that nobody handles; with a selection a losing
    // receive consumes nothing, so all three are handled, and the two reports
    // in the order they were sent.
    watchdog guard(60s, "two sources ready at once");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    auto const p2 = make_idle_peer(f.ctx, 102);
    f.start();
    f.send(peers_updated{{p1, p2}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 2u);

    // Queue everything, then run once.
    f.launcher.end(0);
    f.launcher.end(1);
    REQUIRE(f.input.try_send(block_download_input{block_range_request{201, 300}}));
    f.pump();

    // Whichever source went first, every message took effect: both workers of
    // range A were released and both peers were started for range B.
    REQUIRE(f.launcher.launches.size() == 4u);
    CHECK(f.launcher.count_for_epoch(1) == 2u);
    CHECK(f.launcher.count_for_epoch(2) == 2u);

    // And the slots are owned by B's workers: a third range starts nobody.
    f.send(block_range_request{301, 400});
    CHECK(f.launcher.launches.size() == 4u);
}

TEST_CASE("select: a stop queued behind a flood of worker output is attended within one wait",
          "[node][select_sync][supervisor]") {
    // Control before data. The workers' output is filled to capacity with
    // performance reports — the first of which is taken by the supervisor's
    // suspended wait — and THEN a stop is queued on the input. The selection is
    // biased towards the input, so the supervisor forwards exactly the one
    // report it had already committed to and stops. A unified FIFO queue, or an
    // unbiased arbitration, would forward (most of) the flood first.
    watchdog guard(60s, "a stop behind a flood");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 1u);

    auto const flooded = f.flood_task_output();
    CHECK(flooded == 256u + 1u);   // capacity, plus the one the suspended wait took

    REQUIRE(f.input.try_send(block_download_input{stop_request{}}));
    f.pump();
    f.ctx.restart();
    f.ctx.run_for(5s);

    CHECK(f.supervisor.done.load());
    CHECK(f.forwarded_performance() == 1u);
    f.running = false;   // stopped by this case; the fixture has nothing to drain
    CHECK(f.ctx.stopped());
}

// =============================================================================
// Supervisor: the critical report under load, and at shutdown
// =============================================================================

TEST_CASE("select: a worker's task_ended report waits out a full channel instead of being dropped",
          "[node][select_sync][supervisor]") {
    // The REAL worker reports into a channel that is full, while the supervisor
    // is not yet draining it. The report is a suspended send: it registers,
    // waits its turn behind what is buffered, and lands — in order, after all
    // of it — to free the slot. Nothing is retried, nothing is dropped.
    watchdog guard(60s, "a report under load");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 1u);
    REQUIRE(f.launcher.output != nullptr);

    // Keep the supervisor executor paused while the report executor runs.
    // Fill every buffer slot before the worker initiates its send. An already
    // selected input can retire the output receive before the flood arrives.
    auto worker = f.start_real_worker(0);
    SECTION("no competing control event is queued") {}
    SECTION("a benign peer update wins selection before the flood") {
        // Commit the input alternative without running the supervisor yet.
        // Its output receive is no longer entitled to take the extra item.
        REQUIRE(f.input.try_send(block_download_input{peers_updated{{p1}}}));
    }
    auto const flooded = f.flood_task_output();
    REQUIRE(f.launcher.output->buffered() == 256u);
    // At most one extra report can be committed to the suspended receive.
    // The pending critical send must wait in either selection ordering.
    CHECK(flooded >= 256u);
    CHECK(flooded <= 257u);
    REQUIRE(drain_until(f.report_ctx, [&] { return f.launcher.output->pending() == 1u && ! worker->done.load(); }));

    // Everything ahead of it drains in order; the report lands last and is
    // recognised: the slot is free again, and nothing was lost on the way.
    f.pump();
    REQUIRE(drain_until(f.report_ctx, [&] { return worker->done.load(); }));
    worker->check();
    // The send completes when admitted, before the supervisor consumes the
    // entire prefix. Cooperative selection may need several executor turns.
    REQUIRE(drain_until(f.ctx, [&] { return f.launcher.output->buffered() == 0u; }));
    // Drained, every one of them forwarded. (pending() is not asserted to be
    // zero: the supervisor's own receive registration counts once it waits.)
    CHECK(f.launcher.output->buffered() == 0u);
    CHECK(f.forwarded_performance() == flooded);

    f.send(block_range_request{201, 300});
    REQUIRE(drain_until(f.ctx, [&] { return f.launcher.launches.size() == 2u; }));
    REQUIRE(f.launcher.launches.size() == 2u);
    CHECK(f.launcher.launches[1].epoch == 2u);
}

TEST_CASE("select: shutdown releases a worker suspended on its report and joins everything",
          "[node][select_sync][supervisor][shutdown]") {
    // The same suspended report, but the supervisor is told to stop while it is
    // pending — and attends the stop before the data (biased input first).
    // Closing the workers' output completes that send with broken_pipe; the
    // worker ends; the supervisor's join completes; nothing is left running.
    watchdog guard(60s, "shutdown with a pending report");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 1u);
    REQUIRE(f.launcher.output != nullptr);

    auto worker = f.start_real_worker(0);   // Separate paused executor, as above.
    auto const flooded = f.flood_task_output();
    REQUIRE(drain_until(f.report_ctx, [&] { return f.launcher.output->pending() == 1u && ! worker->done.load(); }));

    REQUIRE(f.input.try_send(block_download_input{stop_request{}}));
    REQUIRE(run_until(f.ctx, [&] { return f.supervisor.done.load(); }));
    f.supervisor.check();
    f.pump();
    REQUIRE(drain_until(f.report_ctx, [&] { return worker->done.load(); }));
    worker->check();

    // Stopped before the flood was drained: what was forwarded is what had been
    // committed before the stop was queued, not the whole flood.
    CHECK(f.forwarded_performance() < flooded);

    f.ctx.restart();
    f.ctx.run_for(5s);
    CHECK(f.ctx.stopped());
    f.running = false;
}

// =============================================================================
// The fast path: a real worker over a real peer, suspended on a full channel
// =============================================================================

namespace {

std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>
make_connected_sockets(::asio::io_context& ctx) {
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto const port = acceptor.local_endpoint().port();
    ::asio::ip::tcp::socket client(ctx);
    ::asio::ip::tcp::socket server(ctx);
    std::thread accept_thread([&] { server = acceptor.accept(); });
    client.connect({::asio::ip::address_v4::loopback(), port});
    accept_thread.join();
    return {std::move(client), std::move(server)};
}

// The remote peer: answers every getdata with the blocks it names, serialized
// exactly as a node would put them on the wire, and can be told to send a ping.
struct remote_peer {
    ::asio::ip::tcp::socket socket;
    std::vector<domain::chain::block> const& blocks;
    uint32_t magic;
    uint32_t version;
    std::atomic<size_t> getdata_count{0};
    std::atomic<size_t> blocks_sent{0};

    remote_peer(::asio::ip::tcp::socket sock, std::vector<domain::chain::block> const& chain_blocks,
                uint32_t protocol_magic, uint32_t protocol_version)
        : socket(std::move(sock)), blocks(chain_blocks), magic(protocol_magic), version(protocol_version) {}

    ::asio::awaitable<void> serve() {
        data_chunk head_buf(domain::message::heading::maximum_size());
        while (true) {
            auto [ec, n] = co_await ::asio::async_read(socket, ::asio::buffer(head_buf),
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return;
            byte_reader head_reader(head_buf);
            auto head = domain::message::heading::from_data(head_reader, 0);
            if ( ! head) co_return;

            data_chunk payload(head->payload_size());
            if ( ! payload.empty()) {
                auto [pec, pn] = co_await ::asio::async_read(socket, ::asio::buffer(payload),
                    ::asio::as_tuple(::asio::use_awaitable));
                if (pec) co_return;
            }

            if (head->command() != domain::message::get_data::command) {
                continue;
            }
            byte_reader reader(payload);
            auto request = domain::message::get_data::from_data(reader, version);
            if ( ! request) co_return;
            ++getdata_count;
            for (auto const& inv : request->inventories()) {
                auto const it = std::ranges::find_if(blocks,
                    [&](auto const& b) { return b.hash() == inv.hash(); });
                if (it == blocks.end()) continue;
                auto const wire = domain::message::serialize(version, domain::message::block(*it), magic);
                auto [wec, wn] = co_await ::asio::async_write(socket, ::asio::buffer(wire),
                    ::asio::as_tuple(::asio::use_awaitable));
                if (wec) co_return;
                ++blocks_sent;
            }
        }
    }

    ::asio::awaitable<void> ping(uint64_t nonce) {
        auto const wire = domain::message::serialize(version, domain::message::ping(nonce), magic);
        auto [wec, wn] = co_await ::asio::async_write(socket, ::asio::buffer(wire),
            ::asio::as_tuple(::asio::use_awaitable));
        (void)wec; (void)wn;
    }
};

// What p2p_node::run_peer_protocols does with the two commands this needs:
// `block` goes to the per-request channel the batch request is reading, and a
// `ping` is observed. Everything else is dropped.
::asio::awaitable<void> route_messages(network::peer_session& peer, std::atomic<size_t>& pings_seen) {
    while (true) {
        auto [ec, raw] = co_await peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) co_return;
        if (raw.heading.command() == domain::message::block::command) {
            auto ex = co_await ::asio::this_coro::executor;
            auto handed = co_await ::asio::experimental::make_select(
                ::asio::experimental::send(peer.block_responses(), std::move(raw))
            ).async_wait(ex, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
            auto& [sec, handed_case] = handed;
            (void)handed_case;
            if (sec) co_return;
        } else if (raw.heading.command() == domain::message::ping::command) {
            ++pings_seen;
        }
    }
}

constexpr uint32_t fast_chunk_size = 2;
constexpr uint32_t fast_block_count = 6;   // three chunks

// The real worker over a real session, with everything it needs owned here.
// Declaration order is destruction order in reverse: the context first, so it
// outlives every object whose operations it may still hold.
struct fast_path_fixture {
    ::asio::io_context ctx;
    test::chain_fixture chain{"select_sync_fast_path"};
    std::vector<domain::chain::block> blocks;
    std::shared_ptr<chunk_coordinator> coordinator;
    fast_validation_input_channel fast_val{1};
    block_download_task_output_channel task_output{8};
    std::atomic<uint32_t> active_peers{0};
    network::settings settings{domain::config::network::regtest};
    network::peer_session::ptr session;
    std::optional<remote_peer> remote;
    std::atomic<size_t> pings_seen{0};
    tracked session_run, router, serve, worker;

    fast_path_fixture() {
        REQUIRE(chain.created());
        REQUIRE(chain.start());

        blocks = test::make_chain_for_select(domain::chain::block::genesis_regtest(), fast_block_count);
        REQUIRE(chain.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());

        coordinator = std::make_shared<chunk_coordinator>(chain.organizer().index(), 1, fast_block_count,
            chunk_coordinator_config{.chunk_size = fast_chunk_size, .slots = 0, .stall_timeout_secs = 3600});

        auto [client, server] = make_connected_sockets(ctx);
        session = std::make_shared<network::peer_session>(std::move(server), settings);
        session->set_nonce(7);
        remote.emplace(std::move(client), blocks, settings.identifier, session->negotiated_version());

        session_run.spawn(ctx, [this]() -> ::asio::awaitable<void> { co_await session->run(); });
        router.spawn(ctx, route_messages(*session, pings_seen));
        serve.spawn(ctx, remote->serve());
    }

    void start_worker() {
        worker.spawn(ctx, block_download_task(session, coordinator, active_peers, task_output,
            /*task_id*/ 11, /*epoch*/ 1, &fast_val));
    }

    // The worker has delivered chunk 0 (buffered, capacity 1) and is suspended
    // on the send of chunk 1: one pending sender, and no third request made.
    bool worker_suspended_on_second_chunk() {
        return fast_val.buffered() == 1 && fast_val.pending() == 1;
    }

    std::optional<downloaded_chunk> take_chunk() {
        std::error_code ec;
        auto msg = fast_val.try_receive(ec);
        if ( ! msg) return std::nullopt;
        REQUIRE(std::holds_alternative<downloaded_chunk>(*msg));
        return std::get<downloaded_chunk>(std::move(*msg));
    }

    std::optional<download_task_ended> take_report() {
        std::error_code ec;
        while (auto msg = task_output.try_receive(ec)) {
            if (auto* ended = std::get_if<download_task_ended>(&*msg)) return *ended;
        }
        return std::nullopt;
    }

    // Stop the session and run everything to its end.
    void finish() {
        session->stop();
        boost_code ignored;
        remote->socket.close(ignored);
        REQUIRE(run_until(ctx, [&] {
            return session_run.done.load() && router.done.load() && serve.done.load() && worker.done.load();
        }));
        session_run.check(); router.check(); serve.check(); worker.check();
        ctx.restart();
        ctx.run_for(5s);
        CHECK(ctx.stopped());
    }
};

} // namespace

TEST_CASE("select: a worker on a full fast channel holds one chunk, keeps the reader live, and is resumed by the receive",
          "[node][select_sync][fast_path]") {
    watchdog guard(120s, "a worker on a full fast channel");
    fast_path_fixture f;
    f.start_worker();

    // Chunk 0 delivered (buffered), chunk 1 downloaded and its send suspended.
    REQUIRE(run_until(f.ctx, [&] { return f.worker_suspended_on_second_chunk(); }));
    f.worker.check();

    // Bounded in-flight ownership: exactly one chunk is buffered, exactly one
    // is held by the suspended worker, and the worker has NOT gone on to claim
    // the third chunk — two requests on the wire, one slot in progress.
    CHECK(f.remote->getdata_count.load() == 2u);
    CHECK(f.remote->blocks_sent.load() == 2u * fast_chunk_size);
    auto progress = f.coordinator->get_progress();
    CHECK(progress.chunks_completed == 1u);
    CHECK(progress.chunks_in_progress == 1u);

    // The peer's reader is not behind the suspended worker: a ping sent now
    // comes out of the session's message channel while the worker still waits.
    f.serve.check();
    tracked ping;
    ping.spawn(f.ctx, f.remote->ping(0xABCD));
    REQUIRE(run_until(f.ctx, [&] { return f.pings_seen.load() == 1u; }));
    CHECK(f.worker_suspended_on_second_chunk());

    // Capacity released by ONE receive: the suspended send completes on that
    // event, not on a timer, and the worker moves on to the third chunk.
    auto first = f.take_chunk();
    REQUIRE(first);
    CHECK(first->start_height == 1u);
    CHECK(first->blocks.size() == fast_chunk_size);
    CHECK(first->generation == f.chain.organizer().index().generation());
    REQUIRE(run_until(f.ctx, [&] { return f.fast_val.pending() == 1 && f.remote->getdata_count.load() == 3u; }));
    CHECK(f.fast_val.buffered() == 1u);   // chunk 1 is buffered now; chunk 2 is held
    CHECK(f.coordinator->get_progress().chunks_completed == 2u);

    auto second = f.take_chunk();
    REQUIRE(second);
    CHECK(second->start_height == 1u + fast_chunk_size);
    REQUIRE(run_until(f.ctx, [&] { return f.fast_val.buffered() == 1 && f.fast_val.pending() == 0; }));
    auto third = f.take_chunk();
    REQUIRE(third);
    CHECK(third->start_height == 1u + 2 * fast_chunk_size);

    // The range is done; the worker leaves on its own and reports it.
    REQUIRE(run_until(f.ctx, [&] { return f.coordinator->is_complete() && f.worker.done.load(); }));
    f.worker.check();
    auto report = f.take_report();
    REQUIRE(report);
    CHECK(report->peer_nonce == 7u);
    CHECK(report->task_id == 11u);
    CHECK(report->coordinator_epoch == 1u);
    CHECK(f.fast_val.pending() == 0u);

    f.finish();
}

TEST_CASE("select: stopping the coordinator releases a suspended worker and returns its chunk to the range",
          "[node][select_sync][fast_path]") {
    watchdog guard(120s, "coordinator stop with a held chunk");
    fast_path_fixture f;
    f.start_worker();
    REQUIRE(run_until(f.ctx, [&] { return f.worker_suspended_on_second_chunk(); }));

    // Cancellation through the coordinator's stop token: the send is aborted
    // with its payload still the worker's, which gives the chunk back (FREE) and
    // leaves without delivering it. Chunk 0 stays buffered for its consumer.
    //
    // Poll-driven: the wake-up must come from the token (a queued cancellation
    // turn), not from the worker's 10 s report tick. A worker that ignored the
    // token would leave the context dry here with the chunk still held.
    f.coordinator->stop();
    REQUIRE(drain_until(f.ctx, [&] { return f.worker.done.load(); }));
    f.worker.check();
    CHECK(f.fast_val.pending() == 0u);
    CHECK(f.fast_val.buffered() == 1u);
    auto progress = f.coordinator->get_progress();
    CHECK(progress.chunks_completed == 1u);
    CHECK(progress.chunks_in_progress == 0u);
    auto report = f.take_report();
    REQUIRE(report);
    CHECK(report->task_id == 11u);

    f.finish();
}

TEST_CASE("select: closing the fast channel at shutdown releases a suspended worker",
          "[node][select_sync][fast_path][shutdown]") {
    watchdog guard(120s, "channel closed with a held chunk");
    fast_path_fixture f;
    f.start_worker();
    REQUIRE(run_until(f.ctx, [&] { return f.worker_suspended_on_second_chunk(); }));

    // What the orchestrator does at shutdown. The pending send completes with
    // broken_pipe and its payload; the worker drops it and ends. What the
    // channel already held is still drainable, then eof. Poll-driven, like the
    // stop above: the close itself is the wake-up, not a tick.
    f.fast_val.close();
    REQUIRE(drain_until(f.ctx, [&] { return f.worker.done.load(); }));
    f.worker.check();
    CHECK(f.fast_val.pending() == 0u);
    CHECK(f.take_chunk().has_value());
    std::error_code ec;
    CHECK_FALSE(f.fast_val.try_receive(ec).has_value());
    CHECK(ec == ::asio::error::eof);
    CHECK(f.take_report().has_value());

    f.finish();
}

TEST_CASE("select: a peer that stops while its chunk is held does not forfeit the chunk",
          "[node][select_sync][fast_path]") {
    watchdog guard(120s, "peer stopped with a held chunk");
    fast_path_fixture f;
    f.start_worker();
    REQUIRE(run_until(f.ctx, [&] { return f.worker_suspended_on_second_chunk(); }));

    // The peer goes away. The blocks it already gave us are valid; the worker
    // keeps waiting for room, delivers, and only then leaves because its peer
    // is gone.
    f.session->stop();
    boost_code ignored;
    f.remote->socket.close(ignored);
    REQUIRE(run_until(f.ctx, [&] { return f.session_run.done.load(); }));
    CHECK(f.worker_suspended_on_second_chunk());
    CHECK_FALSE(f.worker.done.load());

    auto first = f.take_chunk();
    REQUIRE(first);
    REQUIRE(run_until(f.ctx, [&] { return f.worker.done.load(); }));
    f.worker.check();
    CHECK(f.fast_val.buffered() == 1u);
    auto second = f.take_chunk();
    REQUIRE(second);
    CHECK(second->start_height == 1u + fast_chunk_size);
    CHECK(f.coordinator->get_progress().chunks_completed == 2u);
    CHECK(f.take_report().has_value());

    REQUIRE(run_until(f.ctx, [&] { return f.router.done.load() && f.serve.done.load(); }));
    f.ctx.restart();
    f.ctx.run_for(5s);
    CHECK(f.ctx.stopped());
}

// =============================================================================
// Measurement (hidden; informational, no threshold is a verdict)
// =============================================================================
//
// How long a worker suspended on a full fast channel takes to be resumed and
// to land its chunk once the consumer frees one slot, through the real worker,
// a real session and the io_context, with the context driven one handler at a
// time and the clock read around the resume only. The previous code polled
// try_send every 100 ms while full, so a hand-over then waited 0-100 ms after
// the slot was freed; that number is a design constant of the old code, not
// something measured here, and is why no baseline run is reported.

TEST_CASE("select: measure the resume latency of a worker suspended on a full fast channel",
          "[.measure][select_sync]") {
    watchdog guard(300s, "resume latency measurement");
    fast_path_fixture f;
    f.start_worker();

    std::vector<double> resume_us;
    // Three chunks: two hand-overs are measured (chunk 1 and chunk 2).
    for (int i = 0; i < 2; ++i) {
        REQUIRE(run_until(f.ctx, [&] { return f.fast_val.buffered() == 1 && f.fast_val.pending() == 1; }));
        auto const before = std::chrono::steady_clock::now();
        auto taken = f.take_chunk();
        REQUIRE(taken);
        // Run exactly as many handlers as it takes for the suspended send to
        // complete and the chunk to land; nothing else is ready to run.
        while ( ! (f.fast_val.buffered() == 1 && f.fast_val.pending() == 0)) {
            f.ctx.restart();
            f.ctx.run_one();
        }
        auto const after = std::chrono::steady_clock::now();
        resume_us.push_back(std::chrono::duration<double, std::micro>(after - before).count());
    }
    for (size_t i = 0; i < resume_us.size(); ++i) {
        std::fprintf(stderr, "[measure] hand-over %zu: slot freed -> chunk landed in %.1f us\n", i + 1, resume_us[i]);
    }
    REQUIRE(run_until(f.ctx, [&] { return f.fast_val.buffered() == 1 && f.fast_val.pending() == 0; }));
    (void)f.take_chunk();
    REQUIRE(run_until(f.ctx, [&] { return f.coordinator->is_complete() && f.worker.done.load(); }));
    (void)f.take_report();
    f.finish();
}

// =============================================================================
// The consumer: fast_validation_task's receive loop
// =============================================================================

TEST_CASE("select: fast_validation_task drains what is buffered after close and then ends",
          "[node][select_sync][fast_path]") {
    watchdog guard(120s, "fast validation drain");
    test::chain_fixture fixture("select_sync_validation");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const blocks = test::make_chain_for_select(domain::chain::block::genesis_regtest(), 4);

    ::asio::io_context ctx;
    fast_validation_input_channel input(4);
    chunk_validated_channel output(ctx.get_executor(), 16);

    auto chunk_of = [&](uint32_t start, uint32_t count) {
        std::vector<std::shared_ptr<domain::chain::light_block const>> light;
        for (uint32_t h = start; h < start + count; ++h) light.push_back(test::to_light(blocks[h - 1]));
        return fast_validation_input{downloaded_chunk{
            .start_height = start, .chunk_id = start, .blocks = std::move(light),
            .source_peer = nullptr, .generation = fixture.chain().headers().generation()}};
    };
    REQUIRE(input.try_send(chunk_of(1, 2)));
    REQUIRE(input.try_send(chunk_of(3, 2)));
    input.close();   // Buffered chunks are still delivered, then eof.

    tracked task;
    task.spawn(ctx, fast_validation_task(fixture.chain(), input, output, nullptr));
    REQUIRE(run_until(ctx, [&] { return task.done.load(); }));
    task.check();

    std::vector<chunk_validated> results;
    while (output.try_receive([&](std::error_code, chunk_validated r) { results.push_back(r); })) {}
    REQUIRE(results.size() == 2u);
    CHECK(results[0].start_height == 1u);
    CHECK(results[1].start_height == 3u);
    CHECK_FALSE(results[0].result);
    CHECK_FALSE(results[1].result);
    CHECK(input.pending() == 0u);
}

// =============================================================================
// The coordinator's range request
// =============================================================================

TEST_CASE("select: a range request waits for room and is taken by the receive that makes it",
          "[node][select_sync][range]") {
    watchdog guard(60s, "a range request waiting for room");
    test::chain_fixture fixture("select_sync_range");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    ::asio::io_context ctx;
    block_download_input_channel input(1);
    network::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = (fixture.dir() / "hosts_nonexistent.dat").string();
    settings.banlist_file = (fixture.dir() / "banlist_nonexistent.dat").string();
    node::p2p_node network(settings);
    body_range_deps deps{fixture.chain(), network, input, 1};

    // Room: taken at once.
    std::optional<bool> sent;
    tracked first;
    first.spawn(ctx, [&]() -> ::asio::awaitable<void> { sent = co_await send_block_range(deps, 10, 20); });
    REQUIRE(run_until(ctx, [&] { return first.done.load(); }));
    first.check();
    REQUIRE(sent.has_value());
    CHECK(*sent);
    CHECK(input.buffered() == 1u);

    // No room: the request is a pending sender, keeping its payload. ONE
    // receive makes room, and the request lands on that event. Nothing here
    // waits on a clock: the send is armed with the network's stop token, and
    // this network is never stopped.
    sent.reset();
    tracked second;
    second.spawn(ctx, [&]() -> ::asio::awaitable<void> { sent = co_await send_block_range(deps, 21, 30); });
    REQUIRE(run_until(ctx, [&] { return input.pending() == 1u; }));
    CHECK_FALSE(second.done.load());

    std::error_code ec;
    auto taken = input.try_receive(ec);
    REQUIRE(taken);
    REQUIRE(std::holds_alternative<block_range_request>(*taken));
    CHECK(std::get<block_range_request>(*taken).start_height == 10u);
    REQUIRE(run_until(ctx, [&] { return second.done.load(); }));
    second.check();
    REQUIRE(sent.has_value());
    CHECK(*sent);
    auto landed = input.try_receive(ec);
    REQUIRE(landed);
    CHECK(std::get<block_range_request>(*landed).start_height == 21u);

    // Closed while waiting: abandoned, reported as such.
    REQUIRE(input.try_send(block_download_input{block_range_request{1, 2}}));
    sent.reset();
    tracked third;
    third.spawn(ctx, [&]() -> ::asio::awaitable<void> { sent = co_await send_block_range(deps, 31, 40); });
    REQUIRE(run_until(ctx, [&] { return input.pending() == 1u; }));
    input.close();
    REQUIRE(run_until(ctx, [&] { return third.done.load(); }));
    third.check();
    REQUIRE(sent.has_value());
    CHECK_FALSE(*sent);
    CHECK(input.pending() == 0u);
}

TEST_CASE("select: a range request waiting for room is abandoned when the network stops",
          "[node][select_sync][range]") {
    // The other way out of a full channel: the network's stop token. The node
    // publishes its stop as an event now, so a request waiting for room ends on
    // it at once, with no timer re-reading stopped().
    watchdog guard(60s, "a range request abandoned on stop");
    test::chain_fixture fixture("select_sync_range_stop");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    ::asio::io_context ctx;
    block_download_input_channel input(1);
    network::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = (fixture.dir() / "hosts_nonexistent.dat").string();
    settings.banlist_file = (fixture.dir() / "banlist_nonexistent.dat").string();
    node::p2p_node network(settings);
    body_range_deps deps{fixture.chain(), network, input, 1};

    REQUIRE(input.try_send(block_download_input{block_range_request{1, 2}}));
    std::optional<bool> sent;
    tracked waiting;
    waiting.spawn(ctx, [&]() -> ::asio::awaitable<void> { sent = co_await send_block_range(deps, 10, 20); });
    REQUIRE(run_until(ctx, [&] { return input.pending() == 1u; }));
    CHECK_FALSE(waiting.done.load());

    // stop() on a node that was never started: it still requests its token.
    network.stop();
    CHECK(network.stop_token().stop_requested());
    REQUIRE(run_until(ctx, [&] { return waiting.done.load(); }));
    waiting.check();
    REQUIRE(sent.has_value());
    CHECK_FALSE(*sent);
    CHECK(input.pending() == 0u);
    CHECK(input.buffered() == 1u);   // what was there stays; the abandoned request is gone
}
