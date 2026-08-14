// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::node::sync;

// =============================================================================
// A range must reach the real supervisor with consumers (#652)
// =============================================================================
//
// The unit tests next door pin the bookkeeping as values. These drive the actual
// coroutine: the unified events channel, block_range_request, the snapshot of
// known peers, the reservation before a start, task_ended and its exact match,
// the handoff and the rollback. Only ONE thing is substituted — running the
// download itself — because the real worker talks p2p, and what these cases need
// to control is WHEN a worker ends, not what it says on the wire.
//
// The peers are real `peer_session` objects over a socket that was never
// connected: the constructor reads the remote endpoint through the error_code
// overload and falls back to `authority::any()`, so nothing binds, connects or
// starts a background task.

namespace {

// Bounded, and it aborts rather than letting a stall become a CI timeout: a
// supervisor that stops consuming has to produce a named failure.
struct watchdog {
    std::atomic<bool> done{false};
    std::thread barker;

    watchdog(std::chrono::seconds budget, char const* what)
        : barker([this, budget, what] {
              auto const deadline = std::chrono::steady_clock::now() + budget;
              while ( ! done.load()) {
                  if (std::chrono::steady_clock::now() > deadline) {
                      std::fprintf(stderr, "\n[watchdog] %s did not finish: treating it "
                          "as a supervisor defect\n", what);
                      std::abort();
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(10));
              }
          }) {}

    ~watchdog() {
        done.store(true);
        barker.join();
    }
};

// A peer that exists and does nothing. Never connected, so it starts no
// coroutine of its own and the fixture owns its whole lifetime.
network::peer_session::ptr make_idle_peer(::asio::io_context& ctx, uint64_t nonce) {
    network::settings settings(domain::config::network::regtest);
    ::asio::ip::tcp::socket sock(ctx);
    auto peer = std::make_shared<network::peer_session>(std::move(sock), settings);
    // The nonce is the identity the supervisor deduplicates on, and a session
    // that never handshook has it at zero — two of them would be the same peer.
    // Set explicitly so every assertion below names a specific peer.
    peer->set_nonce(nonce);
    return peer;
}

// What the seam saw, and the handle a test uses to end a worker when it chooses.
struct launch_record {
    uint64_t nonce{0};
    uint64_t task_id{0};
    uint64_t epoch{0};
    std::shared_ptr<chunk_coordinator> coordinator;

    // The real worker takes the session BY VALUE and uses it until its last line,
    // so it holds a reference for its whole execution. The seam holds one too, or
    // the lifetime it models would be a shorter one than production has.
    network::peer_session::ptr peer;
};

// Replaces "run the download", and nothing else. Every launch is recorded, and
// each one is ended only when the test says so — which is how an ordering that
// takes 59 seconds on mainnet becomes a value here.
struct recording_launcher {
    std::vector<launch_record> launches;
    block_download_task_output_channel* output{nullptr};
    // Ends the worker from inside the launch, so its report is queued BEFORE
    // spawn_download returns. That is the only way to reach the ordering the
    // reservation exists for: with the report sent afterwards, the slot is
    // already recorded and the case cannot tell the two orders apart.
    bool end_from_inside{false};

    download_worker_launcher make() {
        return [this](network::peer_session::ptr peer,
                      std::shared_ptr<chunk_coordinator> coordinator,
                      uint64_t task_id, uint64_t epoch,
                      block_download_task_output_channel& out) {
            output = &out;
            launches.push_back(launch_record{peer->nonce(), task_id, epoch, coordinator, peer});
            if (end_from_inside) {
                auto& l = launches.back();
                REQUIRE(out.try_send(std::error_code{},
                    download_task_ended{l.nonce, l.task_id, l.epoch}));
                l.peer.reset();
            }
        };
    }

    // Report the end of a launch exactly as the real worker does: the identity it
    // was handed, repeated verbatim.
    void end(size_t index) {
        REQUIRE(index < launches.size());
        REQUIRE(output != nullptr);
        auto& l = launches[index];
        REQUIRE(output->try_send(std::error_code{},
            download_task_ended{l.nonce, l.task_id, l.epoch}));
        // The worker is over, so the reference it was holding goes with it —
        // the coroutine frame is destroyed at the same point in production.
        l.peer.reset();
    }

    [[nodiscard]] size_t count_for_epoch(uint64_t epoch) const {
        return size_t(std::ranges::count_if(launches,
            [epoch](auto const& l) { return l.epoch == epoch; }));
    }
};

// Drives the real supervisor on one io_context, and joins everything it started.
// Nothing may outlive the fixture: a peer, a coroutine or a pending message left
// behind would poison the next case in this binary.
struct supervisor_fixture {
    ::asio::io_context ctx;
    block_download_input_channel input{ctx.get_executor(), 64};
    block_download_channel output{ctx.get_executor(), 256};
    test::chain_fixture chain{"download_supervisor"};
    recording_launcher launcher;
    bool running{false};

    supervisor_fixture() {
        REQUIRE(chain.created());
        REQUIRE(chain.start());
    }

    void start() {
        ::asio::co_spawn(ctx,
            block_download_supervisor(input, output, chain.organizer(), nullptr, launcher.make()),
            ::asio::detached);
        running = true;
        pump();
    }

    // Runs until the supervisor has nothing left to do. Deterministic: it drains
    // the queued work rather than waiting for a clock.
    void pump() {
        ctx.restart();
        ctx.poll();
    }

    void send(block_download_input msg) {
        REQUIRE(input.try_send(std::error_code{}, std::move(msg)));
        pump();
    }

    // Nothing may outlive a case in this binary. Checked rather than assumed:
    // a message left unread would mean the supervisor stopped consuming, and a
    // launch left unended would mean a worker the next case could still see.
    void drain_and_check() {
        if ( ! running) return;
        running = false;

        (void)input.try_send(std::error_code{}, stop_request{});
        pump();
        ctx.restart();
        ctx.run_for(std::chrono::seconds(5));

        bool leftover = false;
        while (input.try_receive([&](std::error_code, block_download_input) { leftover = true; })) {}
        CHECK_FALSE(leftover);            // no input the supervisor never read
        CHECK(ctx.stopped());             // the coroutine finished
    }

    ~supervisor_fixture() {
        drain_and_check();
    }
};

} // namespace

TEST_CASE("1: a range started after the previous one drained gets consumers",
          "[node][download_supervisor]") {
    // The second stall, end to end: every worker of A reported, and B arrives
    // with NO peer event in between. The supervisor must start from the peers it
    // knows rather than from an event that is not coming.
    watchdog guard(std::chrono::seconds(60), "a range after the previous drained");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    auto const p2 = make_idle_peer(f.ctx, 102);
    f.start();
    f.send(peers_updated{{p1, p2}});
    f.send(block_range_request{100, 200});

    REQUIRE(f.launcher.launches.size() == 2u);
    CHECK(f.launcher.count_for_epoch(1) == 2u);

    // Both workers of A report.
    f.launcher.end(0);
    f.pump();
    f.launcher.end(1);
    f.pump();

    // Range B, no peers_updated.
    f.send(block_range_request{201, 300});
    CHECK(f.launcher.launches.size() == 4u);
    CHECK(f.launcher.count_for_epoch(2) == 2u);
}

TEST_CASE("2: a range that replaces a live one starts each peer as it reports",
          "[node][download_supervisor]") {
    // The first stall. At the instant B is installed every slot is still held by
    // A's workers, so nothing can start yet — and the old code had nothing that
    // would ever try again. Each late report is what starts its own peer for B.
    watchdog guard(std::chrono::seconds(60), "a range replacing a live one");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 1u);

    f.send(block_range_request{201, 300});
    CHECK(f.launcher.launches.size() == 1u);   // A's worker still holds the slot

    f.launcher.end(0);
    f.pump();

    REQUIRE(f.launcher.launches.size() == 2u);
    CHECK(f.launcher.launches[1].epoch == 2u);
    CHECK(f.launcher.launches[1].nonce == p1->nonce());
}

TEST_CASE("3: a duplicated report of the old range neither retires nor duplicates the new worker",
          "[node][download_supervisor]") {
    watchdog guard(std::chrono::seconds(60), "a duplicated report");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    f.send(block_range_request{201, 300});

    f.launcher.end(0);
    f.pump();
    REQUIRE(f.launcher.launches.size() == 2u);

    // The same report again — a duplicate, or a retried send.
    f.launcher.end(0);
    f.pump();
    CHECK(f.launcher.launches.size() == 2u);   // no second worker

    // And the worker of B is still owned: a third range starts nobody, because
    // its slot is held.
    f.send(block_range_request{301, 400});
    CHECK(f.launcher.launches.size() == 2u);
}

TEST_CASE("4: two replacements before the first drains start the LAST range",
          "[node][download_supervisor]") {
    watchdog guard(std::chrono::seconds(60), "two replacements");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});   // A
    f.send(block_range_request{201, 300});   // B
    f.send(block_range_request{301, 400});   // C
    REQUIRE(f.launcher.launches.size() == 1u);

    f.launcher.end(0);
    f.pump();

    REQUIRE(f.launcher.launches.size() == 2u);
    CHECK(f.launcher.launches[1].epoch == 3u);   // C, never B
}

TEST_CASE("5: a peer withdrawn before its old worker reports is not started again",
          "[node][download_supervisor]") {
    watchdog guard(std::chrono::seconds(60), "a withdrawn peer");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    auto const p2 = make_idle_peer(f.ctx, 102);
    f.start();
    f.send(peers_updated{{p1, p2}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 2u);

    f.send(block_range_request{201, 300});

    // p1 disappears from the snapshot while its worker is still finishing.
    f.send(peers_updated{{p2}});

    // Both workers of range A report. The withdrawn peer must not come back;
    // the surviving one must be handed over — and asserting only the first half
    // would pass on a supervisor that handed over nothing at all.
    std::optional<size_t> p1_first;
    std::optional<size_t> p2_first;
    for (size_t i = 0; i < f.launcher.launches.size(); ++i) {
        auto const& l = f.launcher.launches[i];
        if (l.epoch != 1) continue;
        if (l.nonce == p1->nonce()) p1_first = i;
        if (l.nonce == p2->nonce()) p2_first = i;
    }
    REQUIRE(p1_first);
    REQUIRE(p2_first);

    f.launcher.end(*p1_first);
    f.pump();
    f.launcher.end(*p2_first);
    f.pump();

    bool p1_after = false;
    bool p2_after = false;
    for (auto const& l : f.launcher.launches) {
        if (l.epoch < 2) continue;
        if (l.nonce == p1->nonce()) p1_after = true;
        if (l.nonce == p2->nonce()) p2_after = true;
    }
    CHECK_FALSE(p1_after);   // withdrawn: never resurrected
    CHECK(p2_after);         // still known: handed to the current range
}

TEST_CASE("6: a worker of the current range releases its slot without a restart",
          "[node][download_supervisor]") {
    // No respawn loop: the message carries no reason, so completion, a stop and a
    // failure are indistinguishable, and restarting on all of them would spin.
    watchdog guard(std::chrono::seconds(60), "a current-range report");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});
    f.send(block_range_request{100, 200});
    REQUIRE(f.launcher.launches.size() == 1u);

    f.launcher.end(0);
    f.pump();
    CHECK(f.launcher.launches.size() == 1u);   // released, not restarted

    // The slot is free, so the next range picks the peer up — which is where a
    // restart belongs.
    f.send(block_range_request{201, 300});
    CHECK(f.launcher.launches.size() == 2u);
}

TEST_CASE("7: a report queued from inside the launch travels the real path",
          "[node][download_supervisor]") {
    // The seam sends the report from INSIDE the launch, so it is queued before
    // the launching handler returns and then travels the whole way: task_output,
    // the bridge, the unified channel, the handler.
    //
    // And it is attended AFTER that handler finishes, necessarily. The supervisor
    // is one coroutine whose only receive is `co_await events.async_receive(...)`;
    // there is no suspension, callback or reentry between starting a worker and
    // the statements that follow it. So an early report cannot overtake the
    // bookkeeping — which is also why claiming the slot first is a defensive
    // protocol rather than the repair of a race, and why a mutation that records
    // after the launch leaves every case in this file green.
    watchdog guard(std::chrono::seconds(60), "a report from inside the launch");
    supervisor_fixture f;

    auto const p1 = make_idle_peer(f.ctx, 101);
    f.start();
    f.send(peers_updated{{p1}});

    f.launcher.end_from_inside = true;
    f.send(block_range_request{100, 200});
    f.launcher.end_from_inside = false;

    REQUIRE(f.launcher.launches.size() == 1u);
    f.pump();

    // Recognised and released, through the real channel rather than a direct
    // call: the peer is startable again.
    f.send(block_range_request{201, 300});
    REQUIRE(f.launcher.launches.size() == 2u);
    CHECK(f.launcher.launches[1].epoch == 2u);
}

TEST_CASE("dropping a peer from the snapshot releases only the supervisor's reference",
          "[node][download_supervisor][lifetime]") {
    // This design DOES introduce a lifetime the old one did not have:
    // `known_peers` holds a session BETWEEN events, where `pending_peers` was
    // cleared the moment it was used. A supervisor that churned through peers
    // for hours would otherwise accumulate every session it ever saw.
    //
    // But "the supervisor stopped holding it" is not "the object was destroyed",
    // and the difference matters here: the real worker takes the session by value
    // and uses it until its last line, so while one is running it holds a
    // reference of its own. The four properties are separated below rather than
    // collapsed into one assertion that would only be true because a seam
    // released earlier than production does.
    watchdog guard(std::chrono::seconds(60), "the snapshot reference");
    supervisor_fixture f;

    std::weak_ptr<network::peer_session> observer;
    {
        auto peer = make_idle_peer(f.ctx, 101);
        observer = peer;
        f.start();
        f.send(peers_updated{{peer}});
    }
    REQUIRE_FALSE(observer.expired());   // only the supervisor holds it now

    // No range, so no worker ever took a reference: dropping it from the
    // snapshot is the ONLY reference, and the session goes.
    f.send(peers_updated{{}});
    CHECK(observer.expired());
}

TEST_CASE("a peer removed from the snapshot outlives it while a worker still holds it",
          "[node][download_supervisor][lifetime]") {
    // The property the previous version of this test got wrong. With a worker
    // running, removing the peer from the snapshot must NOT destroy it — the
    // worker is still using it — and it must go once that worker ends, without
    // needing any further event.
    watchdog guard(std::chrono::seconds(60), "a peer held by a live worker");
    supervisor_fixture f;

    std::weak_ptr<network::peer_session> observer;
    {
        auto peer = make_idle_peer(f.ctx, 101);
        observer = peer;
        f.start();
        f.send(peers_updated{{peer}});
        f.send(block_range_request{100, 200});
    }
    REQUIRE(f.launcher.launches.size() == 1u);
    REQUIRE_FALSE(observer.expired());

    // Withdrawn: the supervisor releases its reference, the worker does not.
    f.send(peers_updated{{}});
    CHECK_FALSE(observer.expired());   // still alive, and correctly so

    // The worker ends. Its reference goes with it, and the report is processed
    // in the same pump — no other event is needed.
    f.launcher.end(0);
    f.pump();
    CHECK(observer.expired());
}

TEST_CASE("shutting the supervisor down releases the peers it still knows",
          "[node][download_supervisor][lifetime]") {
    // The other end of the same question: a supervisor that stops while its
    // snapshot is NOT empty must not leave those sessions alive. They would
    // outlive the node's sync layer with nothing left to release them.
    watchdog guard(std::chrono::seconds(60), "the shutdown release");

    std::weak_ptr<network::peer_session> a;
    std::weak_ptr<network::peer_session> b;
    {
        supervisor_fixture f;
        {
            auto p1 = make_idle_peer(f.ctx, 101);
            auto p2 = make_idle_peer(f.ctx, 102);
            a = p1;
            b = p2;
            f.start();
            f.send(peers_updated{{p1, p2}});
            f.send(block_range_request{100, 200});
        }
        REQUIRE_FALSE(a.expired());
        REQUIRE_FALSE(b.expired());

        // Stopped with both still known AND both still working. The supervisor
        // must release what it knows; what the workers hold is theirs until they
        // end, so the seam ends them here — anything still alive afterwards
        // could only be a reference nobody owns.
        f.launcher.end(0);
        f.launcher.end(1);
        f.drain_and_check();
    }

    CHECK(a.expired());
    CHECK(b.expired());
}
