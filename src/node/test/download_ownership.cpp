// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>

#include <array>
#include <vector>

#include <kth/node/sync/download_ownership.hpp>

using namespace kth;
using namespace kth::node::sync;

// =============================================================================
// A range must never be left without consumers (#652)
// =============================================================================
//
// The defect, as a mainnet node performed it twice:
//
//   13:17:12  range 963888-963893 created — nobody claims it
//             ── 12m43s ──
//   13:29:55  a peer DISCONNECTS -> peers_updated -> 8 workers spawned
//   13:30:17  the chunk downloads in 355 ms
//
//   13:38:12  range 963894-963896 created, previous workers already finished
//             ── 10m40s ──
//   13:48:53  another disconnect -> 8 workers spawned
//   13:48:53  the chunk finishes ~214 ms later
//
// Progress depended on an unrelated peer event. These pin the bookkeeping that
// removes that dependency, in the orderings the coroutine can actually produce —
// as values, so a verdict is a count and an epoch rather than a wait.
//
// Ordering matters more than concurrency here: the supervisor's loop is serial
// (one coroutine over one unified channel), so no two handlers overlap. What it
// cannot prevent is a LATE event — replacing a coordinator does not stop its
// workers, and one mid-download took 59 seconds to come back — so every case
// below is about who owns a slot when a stale report finally lands.

namespace {

// Deterministic identities. The assertions are about exact ids and epochs, so
// nothing here may depend on ordering inside a hash container.
constexpr uint64_t peer_a = 11;
constexpr uint64_t peer_b = 22;
constexpr uint64_t peer_c = 33;

constexpr bool work_pending = true;
constexpr bool no_work = false;

std::vector<uint64_t> sorted(std::vector<uint64_t> v) {
    std::ranges::sort(v);
    return v;
}

// Start every peer the range asked for, with ids the test chooses.
void start_all(download_ownership& own, std::vector<uint64_t> const& nonces,
               uint64_t first_task_id) {
    uint64_t id = first_task_id;
    for (auto const nonce : nonces) {
        own.record(nonce, id++, own.epoch());
    }
}

} // namespace

TEST_CASE("A1: a new range starts consumers from the peers already known",
          "[node][download_ownership]") {
    // The second observed stall: the previous range had finished, every worker
    // had reported, and the supervisor held nothing. The peers were connected
    // and idle the whole time.
    download_ownership own;
    std::array<uint64_t, 3> const peers{peer_a, peer_b, peer_c};
    own.set_known(peers);

    auto const first = own.begin_range();
    CHECK(own.epoch() == 1u);
    CHECK(sorted(first) == std::vector<uint64_t>{peer_a, peer_b, peer_c});
    start_all(own, first, 100);
    REQUIRE(own.worker_count() == 3u);

    // Every worker of range A reports, exactly as the run showed
    // ("remaining downloading=0").
    CHECK_FALSE(own.ended(peer_a, 100, 1, work_pending));
    CHECK_FALSE(own.ended(peer_b, 101, 1, work_pending));
    CHECK_FALSE(own.ended(peer_c, 102, 1, work_pending));
    REQUIRE(own.worker_count() == 0u);

    // Range B, with NO peer event in between. This is the assertion the defect
    // fails: the three peers are still known, so all three start.
    auto const second = own.begin_range();
    CHECK(own.epoch() == 2u);
    CHECK(sorted(second) == std::vector<uint64_t>{peer_a, peer_b, peer_c});
}

TEST_CASE("A2: a range that arrives while the previous workers are still leaving",
          "[node][download_ownership]") {
    // The first observed stall, and the case a peers-list alone does not fix:
    // replacing the coordinator does not stop its workers, so at the instant B
    // is installed every slot is still held. Starting B then would be refused —
    // and nothing would ever try again.
    //
    // Each late report is what starts its own peer against B: one worker per
    // report, no more.
    download_ownership own;
    std::array<uint64_t, 2> const peers{peer_a, peer_b};
    own.set_known(peers);

    auto const first = own.begin_range();
    start_all(own, first, 200);
    REQUIRE(own.worker_count() == 2u);

    // B arrives with both workers of A still running.
    auto const second = own.begin_range();
    CHECK(own.epoch() == 2u);
    CHECK(second.empty());          // nothing can start yet: both slots are held

    // A's workers report, one at a time. Each hands back exactly one start.
    auto const restart_a = own.ended(peer_a, 200, 1, work_pending);
    REQUIRE(restart_a);
    CHECK(*restart_a == peer_a);
    own.record(peer_a, 300, own.epoch());

    auto const restart_b = own.ended(peer_b, 201, 1, work_pending);
    REQUIRE(restart_b);
    CHECK(*restart_b == peer_b);
    own.record(peer_b, 301, own.epoch());

    // Both peers now belong to B, under the current epoch.
    CHECK(own.worker_count() == 2u);
    REQUIRE(own.slot_of(peer_a));
    CHECK(own.slot_of(peer_a)->epoch == 2u);
    CHECK(own.slot_of(peer_a)->task_id == 300u);
    REQUIRE(own.slot_of(peer_b));
    CHECK(own.slot_of(peer_b)->epoch == 2u);
}

TEST_CASE("A3: a late or duplicated report never retires the worker that replaced it",
          "[node][download_ownership]") {
    // The ABA. `download_task_ended` carries the peer's nonce, and a nonce alone
    // cannot tell two workers for that peer apart. Without the task id and the
    // epoch, the second copy of A's report would retire B's worker and leave the
    // peer with no consumer and no event coming.
    download_ownership own;
    std::array<uint64_t, 1> const peers{peer_a};
    own.set_known(peers);

    auto const first = own.begin_range();
    start_all(own, first, 400);

    auto const second = own.begin_range();
    CHECK(second.empty());

    auto const restart = own.ended(peer_a, 400, 1, work_pending);
    REQUIRE(restart);
    own.record(peer_a, 401, own.epoch());
    REQUIRE(own.slot_of(peer_a) == download_slot{401, 2});

    // The SAME report again — a duplicate, or a retry of the send.
    CHECK_FALSE(own.ended(peer_a, 400, 1, work_pending));
    CHECK(own.worker_count() == 1u);
    CHECK(own.slot_of(peer_a) == download_slot{401, 2});   // untouched

    // And a report with the right epoch but the wrong instance.
    CHECK_FALSE(own.ended(peer_a, 999, 2, work_pending));
    CHECK(own.slot_of(peer_a) == download_slot{401, 2});
}

TEST_CASE("A4: two replacements before the first range drains start the LAST range",
          "[node][download_ownership]") {
    // A -> B -> C while A's worker is still finishing. When it finally reports,
    // the peer must be started for C. Starting it for B would attach a consumer
    // to a coordinator already replaced — work nobody will collect.
    download_ownership own;
    std::array<uint64_t, 1> const peers{peer_a};
    own.set_known(peers);

    auto const a = own.begin_range();
    start_all(own, a, 500);

    CHECK(own.begin_range().empty());   // B
    CHECK(own.begin_range().empty());   // C
    CHECK(own.epoch() == 3u);

    auto const restart = own.ended(peer_a, 500, 1, work_pending);
    REQUIRE(restart);
    own.record(peer_a, 501, own.epoch());

    REQUIRE(own.slot_of(peer_a));
    CHECK(own.slot_of(peer_a)->epoch == 3u);   // C, not B
}

TEST_CASE("A5: a peer withdrawn before its old worker reports is not started again",
          "[node][download_ownership]") {
    // The snapshot is the authority on who exists. A peer dropped from it is
    // gone, and its worker still on the way out must not resurrect it — the
    // supervisor would be starting a session the network layer has retired.
    download_ownership own;
    std::array<uint64_t, 2> const both{peer_a, peer_b};
    own.set_known(both);

    auto const first = own.begin_range();
    start_all(own, first, 600);
    CHECK(own.begin_range().empty());   // range B, both slots still held

    // peer_a disappears from the snapshot.
    std::array<uint64_t, 1> const remaining{peer_b};
    own.set_known(remaining);
    CHECK_FALSE(own.knows(peer_a));

    // Its old worker reports: the slot is released, and nothing is started.
    CHECK_FALSE(own.ended(peer_a, 600, 1, work_pending));
    CHECK_FALSE(own.has_worker(peer_a));

    // The peer that remains is started as usual, so the refusal above is about
    // withdrawal and not about the epoch.
    auto const restart_b = own.ended(peer_b, 601, 1, work_pending);
    REQUIRE(restart_b);
    CHECK(*restart_b == peer_b);
}

TEST_CASE("A6: a worker of the CURRENT range is not restarted blindly",
          "[node][download_ownership]") {
    // A worker of the live coordinator ends for reasons this message cannot
    // distinguish: the range completed, the peer failed, a transient error. It
    // carries no reason, so restarting on all of them is a spin — a worker that
    // exits immediately would be restarted immediately, forever.
    //
    // The slot is released so a later range can use it, and nothing else.
    download_ownership own;
    std::array<uint64_t, 1> const peers{peer_a};
    own.set_known(peers);

    auto const first = own.begin_range();
    start_all(own, first, 700);

    CHECK_FALSE(own.ended(peer_a, 700, 1, work_pending));
    CHECK(own.worker_count() == 0u);
    CHECK(own.epoch() == 1u);

    // The next range picks it up, which is where a restart belongs.
    auto const second = own.begin_range();
    CHECK(second == std::vector<uint64_t>{peer_a});
}

TEST_CASE("a coordinator with nothing left to do starts nobody",
          "[node][download_ownership]") {
    // The old worker's report hands back a start only while there is a
    // coordinator that wants one. Completed or stopped, it does not.
    download_ownership own;
    std::array<uint64_t, 1> const peers{peer_a};
    own.set_known(peers);

    auto const first = own.begin_range();
    start_all(own, first, 800);
    CHECK(own.begin_range().empty());

    CHECK_FALSE(own.ended(peer_a, 800, 1, no_work));
    CHECK(own.worker_count() == 0u);
}

TEST_CASE("E: a peer arriving mid-range is taken on normally",
          "[node][download_ownership]") {
    // The path that used to be the ONLY way a range got consumers must keep
    // working: a peers_updated during a range adds the new peer, and the ones
    // already running are left alone.
    download_ownership own;
    std::array<uint64_t, 1> const first_snapshot{peer_a};
    own.set_known(first_snapshot);

    auto const first = own.begin_range();
    start_all(own, first, 900);
    REQUIRE(own.worker_count() == 1u);

    std::array<uint64_t, 2> const grown{peer_a, peer_b};
    own.set_known(grown);
    CHECK(own.known_count() == 2u);

    // The supervisor starts the ones without a slot; peer_a keeps the worker it
    // has rather than getting a second one.
    CHECK(own.has_worker(peer_a));
    CHECK_FALSE(own.has_worker(peer_b));
    own.record(peer_b, 901, own.epoch());
    CHECK(own.worker_count() == 2u);
    CHECK(own.slot_of(peer_b) == download_slot{901, 1});
}

TEST_CASE("the peer list is a snapshot, so absence is withdrawal",
          "[node][download_ownership]") {
    // Traced to the producer before relying on it: peer_provider keeps ONE
    // cumulative vector, pushes on connect, erases on disconnect or error,
    // prunes stopped peers, and broadcasts the WHOLE list on every change
    // (orchestrator.cpp, broadcast_peers). So a nonce missing from an update has
    // been withdrawn, and replacing the set is right.
    //
    // With one consequence worth stating: the block channel receives
    // `fast_peers`, which excludes peers classified slow. Such a peer leaves the
    // snapshot without having disconnected, and is therefore not started for
    // downloads — correct, and the reason this set means "eligible to download"
    // rather than "connected".
    download_ownership own;

    std::array<uint64_t, 3> const first{peer_a, peer_b, peer_c};
    own.set_known(first);
    CHECK(own.known_count() == 3u);

    // A snapshot naming fewer peers RETIRES the rest; it does not add to them.
    std::array<uint64_t, 1> const second{peer_b};
    own.set_known(second);
    CHECK(own.known_count() == 1u);
    CHECK(own.knows(peer_b));
    CHECK_FALSE(own.knows(peer_a));
    CHECK_FALSE(own.knows(peer_c));

    // And a range started after it covers exactly the survivors.
    auto const to_start = own.begin_range();
    CHECK(to_start == std::vector<uint64_t>{peer_b});
}

TEST_CASE("an immediate report of an OLD range still hands the peer over exactly once",
          "[node][download_ownership]") {
    // The same race, on the path that matters most: the worker belongs to a
    // range already replaced, so its report is what starts the peer for the
    // current one. Losing it to a phantom slot would leave the new range with no
    // consumer — the defect, reintroduced by an ordering mistake.
    download_ownership own;
    std::array<uint64_t, 1> const peers{peer_a};
    own.set_known(peers);

    auto const first = own.begin_range();
    own.record(peer_a, 1100, own.epoch());

    CHECK(own.begin_range().empty());   // range B, slot still held
    CHECK(own.epoch() == 2u);

    auto const handoff = own.ended(peer_a, 1100, 1, work_pending);
    REQUIRE(handoff);
    CHECK(*handoff == peer_a);

    // Exactly one: the reservation for B succeeds, and a second attempt for the
    // same peer is refused rather than doubling up.
    own.record(peer_a, 1101, own.epoch());
    CHECK(own.has_worker(peer_a));
    CHECK(own.worker_count() == 1u);
    CHECK(own.slot_of(peer_a) == download_slot{1101, 2});
}

