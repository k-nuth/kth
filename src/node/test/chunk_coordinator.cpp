// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <kth/node/sync/chunk_coordinator.hpp>

using namespace kth;
using namespace kth::node::sync;
using namespace std::chrono_literals;

// The coordinator has one owner and reads no clock of its own when it is given
// one: every case below is a sequence of calls, with the time each happens at.

namespace {

using time_point = chunk_coordinator::time_point;

// An arbitrary instant; the cases move on from it.
time_point const t0 = chunk_coordinator::clock::now();

// A worker as the coordinator sees it: the channel its answers arrive on.
struct worker {
    chunk_reply_channel_ptr reply{std::make_shared<chunk_reply_channel>(1)};

    // The answer, if one has arrived. The outer optional is "answered at all".
    std::optional<chunk_reply> answer() {
        ::asio::error_code ec;
        return reply->try_receive(ec);
    }
};

} // namespace

// =============================================================================
// Chunk state
// =============================================================================

TEST_CASE("chunk_coordinator claims each chunk once, in order", "[chunk_coordinator]") {
    // 10 blocks per chunk -> 10 chunks, the last one short (91-95).
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 95, cfg);

    REQUIRE(coord.get_progress().total_chunks == 10);

    std::vector<chunk_claim> claims;
    for (uint32_t expected = 0; expected < 10; ++expected) {
        auto const chunk = coord.claim_chunk(t0);
        REQUIRE(chunk);
        REQUIRE(chunk->chunk_id == expected);
        claims.push_back(*chunk);
    }
    CHECK(coord.chunk_range(0) == std::pair<uint32_t, uint32_t>{1, 10});
    CHECK(coord.chunk_range(9) == std::pair<uint32_t, uint32_t>{91, 95});

    // Every chunk is in progress: nothing to claim, and nothing is complete.
    REQUIRE_FALSE(coord.claim_chunk(t0).has_value());
    CHECK(coord.get_progress().chunks_in_progress == 10);
    CHECK_FALSE(coord.is_complete());

    for (uint32_t id = 0; id < 10; ++id) {
        CHECK_FALSE(coord.is_complete());
        coord.chunk_completed(claims[id]);
        CHECK(coord.get_progress().chunks_completed == id + 1);
    }
    REQUIRE(coord.is_complete());
    REQUIRE_FALSE(coord.claim_chunk(t0).has_value());
}

TEST_CASE("chunk_coordinator one slot per chunk - no blocking", "[chunk_coordinator]") {
    // 16 chunks for 160 blocks / 10
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 160, cfg);

    // Should be able to claim all 16 chunks without blocking
    std::vector<chunk_claim> claimed;
    for (int i = 0; i < 20; ++i) {
        auto c = coord.claim_chunk(t0);
        if (c) claimed.push_back(*c);
    }

    REQUIRE(claimed.size() == 16);

    // Complete all
    for (auto const& claim : claimed) {
        coord.chunk_completed(claim);
    }
    REQUIRE(coord.is_complete());
}

TEST_CASE("chunk_coordinator timeout resets stalled slots", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 20, cfg);

    auto c0 = coord.claim_chunk(t0);
    auto c1 = coord.claim_chunk(t0);
    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());
    REQUIRE_FALSE(coord.claim_chunk(t0).has_value());

    // Not yet: the requests have been pending for less than the timeout.
    coord.check_timeouts(t0 + 999ms);
    REQUIRE_FALSE(coord.claim_chunk(t0 + 999ms).has_value());

    coord.check_timeouts(t0 + 1s);

    // Slots should be free now
    auto c2 = coord.claim_chunk(t0 + 1s);
    REQUIRE(c2.has_value());
}

TEST_CASE("chunk_coordinator timeout leaves alone a chunk claimed after the check read the clock", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 15};
    chunk_coordinator coord(1, 10, cfg);

    // The check is given a clock reading older than the claim.
    auto const read_by_the_check = t0;
    auto const claim = coord.claim_chunk(t0 + 5ms);
    REQUIRE(claim.has_value());

    coord.check_timeouts(read_by_the_check);

    // The chunk is still the peer's: nobody else is handed it.
    REQUIRE_FALSE(coord.claim_chunk(t0 + 5ms).has_value());
    REQUIRE(coord.get_progress().chunks_in_progress == 1);

    coord.chunk_completed(*claim);
    REQUIRE(coord.is_complete());
}

TEST_CASE("chunk_coordinator counts a chunk once when two peers complete it", "[chunk_coordinator]") {
    // Two chunks, 1-10 and 11-20.
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 20, cfg);

    // A slow peer takes the first chunk and stalls; the chunk is handed to a
    // second peer, and then both finish it.
    auto const slow = coord.claim_chunk(t0);
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    coord.check_timeouts(t0 + 1s);
    auto const second = coord.claim_chunk(t0 + 1s);
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_completed(*slow);
    coord.chunk_completed(*second);

    // The second chunk was never downloaded.
    CHECK(coord.get_progress().chunks_completed == 1);
    CHECK(coord.get_progress().chunks_in_progress == 0);
    CHECK_FALSE(coord.is_complete());

    auto const last = coord.claim_chunk(t0 + 1s);
    REQUIRE(last);
    REQUIRE(last->chunk_id == 1u);
    coord.chunk_completed(*last);
    CHECK(coord.get_progress().chunks_completed == 2);
    CHECK(coord.is_complete());
}

TEST_CASE("chunk_coordinator chunk_failed resets slot to FREE", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 20, cfg);

    auto c0 = coord.claim_chunk(t0);
    auto c1 = coord.claim_chunk(t0);
    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());
    REQUIRE_FALSE(coord.claim_chunk(t0).has_value());

    // Fail one - the same chunk becomes claimable again
    coord.chunk_failed(*c0, t0);
    auto c2 = coord.claim_chunk(t0);
    REQUIRE(c2.has_value());
    REQUIRE(c2->chunk_id == c0->chunk_id);
    CHECK(c2->number != c0->number);
}

TEST_CASE("chunk_coordinator keeps a chunk completed when a late claimant fails it", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 20, cfg);

    // The first chunk stalls and is handed to a second peer; the slow peer
    // then delivers it and the second peer fails.
    auto const slow = coord.claim_chunk(t0);
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    coord.check_timeouts(t0 + 1s);
    auto const second = coord.claim_chunk(t0 + 1s);
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_completed(*slow);
    coord.chunk_failed(*second, t0 + 1s);

    // The delivered chunk is not offered again: the next claim is the other one.
    CHECK(coord.get_progress().chunks_completed == 1);
    auto const next = coord.claim_chunk(t0 + 1s);
    REQUIRE(next);
    REQUIRE(next->chunk_id == 1u);
    coord.chunk_completed(*next);
    CHECK(coord.is_complete());
}

TEST_CASE("chunk_coordinator keeps the second peer's claim when the first peer fails late", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 30, cfg);

    // The first chunk stalls and is handed to a second peer. The slow peer's
    // request then fails, which is what a stalled request usually ends in.
    auto const slow = coord.claim_chunk(t0);
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    coord.check_timeouts(t0 + 1s);
    auto const second = coord.claim_chunk(t0 + 1s);
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_failed(*slow, t0 + 1s);

    // The chunk is still the second peer's: a third peer gets the next one
    // instead of downloading the first a third time.
    CHECK(coord.get_progress().chunks_in_progress == 1);
    auto const third = coord.claim_chunk(t0 + 1s);
    REQUIRE(third);
    REQUIRE(third->chunk_id == 1u);

    coord.chunk_completed(*second);
    CHECK(coord.get_progress().chunks_completed == 1);
}

TEST_CASE("chunk_coordinator does not take a downloaded chunk for a stalled one", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 30, cfg);

    // Two chunks claimed; the first one's blocks arrive, the second's do not.
    auto const arrived = coord.claim_chunk(t0);
    auto const pending = coord.claim_chunk(t0);
    REQUIRE(arrived);
    REQUIRE(pending);
    coord.chunk_downloaded(*arrived);

    coord.check_timeouts(t0 + 1h);

    // Only the request still pending was given back.
    CHECK(coord.get_progress().chunks_in_progress == 1);
    auto const again = coord.claim_chunk(t0 + 1h);
    REQUIRE(again);
    CHECK(again->chunk_id == pending->chunk_id);

    // The holder of the downloaded chunk can still give it back or deliver it.
    coord.chunk_failed(*arrived, t0 + 1h);
    auto const retried = coord.claim_chunk(t0 + 1h);
    REQUIRE(retried);
    CHECK(retried->chunk_id == arrived->chunk_id);
}

TEST_CASE("chunk_coordinator ignores a download report from a claim that was handed on", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 20, cfg);

    auto const slow = coord.claim_chunk(t0);
    REQUIRE(slow);
    coord.check_timeouts(t0 + 1s);
    auto const second = coord.claim_chunk(t0 + 1s);
    REQUIRE(second);
    REQUIRE(second->chunk_id == slow->chunk_id);

    // The slow peer's blocks arrive after all. That says nothing about the
    // second peer's request, which is still pending and can still stall.
    coord.chunk_downloaded(*slow);
    coord.check_timeouts(t0 + 2s);

    auto const third = coord.claim_chunk(t0 + 2s);
    REQUIRE(third);
    CHECK(third->chunk_id == slow->chunk_id);
}

TEST_CASE("chunk_coordinator offers a chunk given back below the completed ones", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 50, cfg);

    std::vector<chunk_claim> claims;
    for (int i = 0; i < 4; ++i) {
        auto const claim = coord.claim_chunk(t0);
        REQUIRE(claim);
        claims.push_back(*claim);
    }

    // 0 and 1 completed, 2 given back, 3 completed: the search for a free
    // chunk starts past the completed prefix and still finds chunk 2 first.
    coord.chunk_completed(claims[0]);
    coord.chunk_completed(claims[1]);
    coord.chunk_completed(claims[3]);
    coord.chunk_failed(claims[2], t0);

    auto const retry = coord.claim_chunk(t0);
    REQUIRE(retry);
    CHECK(retry->chunk_id == 2u);
    auto const fresh = coord.claim_chunk(t0);
    REQUIRE(fresh);
    CHECK(fresh->chunk_id == 4u);
    REQUIRE_FALSE(coord.claim_chunk(t0).has_value());

    coord.chunk_completed(*retry);
    coord.chunk_completed(*fresh);
    CHECK(coord.is_complete());
    CHECK(coord.get_progress().chunks_in_progress == 0);
}

TEST_CASE("chunk_coordinator counts a stalled chunk its first peer delivers before it is claimed again", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, 20, cfg);

    auto const slow = coord.claim_chunk(t0);
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    coord.check_timeouts(t0 + 1s);
    CHECK(coord.get_progress().chunks_in_progress == 0);

    // Freed by the timeout, delivered before anyone else took it.
    coord.chunk_completed(*slow);
    CHECK(coord.get_progress().chunks_completed == 1);
    CHECK(coord.get_progress().chunks_in_progress == 0);

    auto const next = coord.claim_chunk(t0 + 1s);
    REQUIRE(next);
    REQUIRE(next->chunk_id == 1u);
}

TEST_CASE("chunk_coordinator ignores reports for chunks outside the range", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 20, cfg);

    coord.chunk_downloaded({.chunk_id = 2, .number = 1});
    coord.chunk_completed({.chunk_id = 2, .number = 1});
    coord.chunk_failed({.chunk_id = 2, .number = 1}, t0);
    coord.chunk_completed({.chunk_id = 1000, .number = 1});

    CHECK(coord.get_progress().chunks_completed == 0);
    CHECK(coord.get_progress().chunks_in_progress == 0);
    CHECK_FALSE(coord.is_complete());
}

// =============================================================================
// The moment a pending request stalls
// =============================================================================

TEST_CASE("chunk_coordinator names the moment its oldest pending request stalls", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 15};
    chunk_coordinator coord(1, 30, cfg);

    // Nothing is pending: there is nothing to wait for.
    CHECK_FALSE(coord.next_deadline().has_value());

    auto const first = coord.claim_chunk(t0);
    auto const second = coord.claim_chunk(t0 + 2s);
    auto const third = coord.claim_chunk(t0 + 5s);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(third);
    CHECK(coord.next_deadline() == t0 + 15s);

    // A request that was answered no longer has a deadline.
    coord.chunk_downloaded(*first);
    CHECK(coord.next_deadline() == t0 + 17s);

    // Nor one whose chunk was given back or delivered.
    coord.chunk_failed(*second, t0 + 6s);
    CHECK(coord.next_deadline() == t0 + 20s);
    coord.chunk_completed(*third);
    CHECK_FALSE(coord.next_deadline().has_value());

    // A chunk claimed again waits a full timeout from its new claim.
    auto const again = coord.claim_chunk(t0 + 6s);
    REQUIRE(again);
    CHECK(again->chunk_id == second->chunk_id);
    CHECK(coord.next_deadline() == t0 + 21s);

    // At the deadline the check frees it, and nothing is pending again.
    coord.check_timeouts(t0 + 21s);
    CHECK_FALSE(coord.next_deadline().has_value());
    CHECK(coord.get_progress().chunks_in_progress == 1);   // the downloaded one
}

// =============================================================================
// Requests from workers
// =============================================================================

TEST_CASE("chunk_coordinator answers a request at once while chunks are free", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 25, cfg);

    worker a, b;
    coord.request_chunk(a.reply, t0);
    coord.request_chunk(b.reply, t0);

    auto const first = a.answer();
    auto const second = b.answer();
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->has_value());
    REQUIRE(second->has_value());
    CHECK(**first == chunk_assignment{.claim = {.chunk_id = 0, .number = 1}, .start_height = 1, .end_height = 10});
    CHECK(**second == chunk_assignment{.claim = {.chunk_id = 1, .number = 1}, .start_height = 11, .end_height = 20});
    CHECK(coord.waiting() == 0);
}

TEST_CASE("chunk_coordinator keeps a request waiting and answers it when a chunk is given back", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 20, cfg);

    worker a, b, idle;
    coord.request_chunk(a.reply, t0);
    coord.request_chunk(b.reply, t0);
    auto const held_by_a = a.answer();
    REQUIRE(held_by_a);
    REQUIRE(held_by_a->has_value());

    // Every chunk is held: the third worker gets no answer, and waits.
    coord.request_chunk(idle.reply, t0);
    CHECK_FALSE(idle.answer().has_value());
    CHECK(coord.waiting() == 1);

    // The chunk given back goes straight to it: no one has to ask again.
    coord.chunk_failed((*held_by_a)->claim, t0 + 1s);
    auto const answered = idle.answer();
    REQUIRE(answered);
    REQUIRE(answered->has_value());
    CHECK((*answered)->claim.chunk_id == (*held_by_a)->claim.chunk_id);
    CHECK((*answered)->claim.number != (*held_by_a)->claim.number);
    CHECK(coord.waiting() == 0);
    CHECK(coord.get_progress().chunks_in_progress == 2);
}

TEST_CASE("chunk_coordinator answers a waiting request when a pending one stalls", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 15};
    chunk_coordinator coord(1, 10, cfg);

    worker slow, idle;
    coord.request_chunk(slow.reply, t0);
    auto const held = slow.answer();
    REQUIRE(held);
    REQUIRE(held->has_value());
    coord.request_chunk(idle.reply, t0 + 1s);
    CHECK_FALSE(idle.answer().has_value());

    REQUIRE(coord.next_deadline() == t0 + 15s);
    coord.check_timeouts(t0 + 15s);

    auto const answered = idle.answer();
    REQUIRE(answered);
    REQUIRE(answered->has_value());
    CHECK((*answered)->claim.chunk_id == 0u);
    // The chunk is pending again, for its new holder.
    CHECK(coord.next_deadline() == t0 + 30s);
}

TEST_CASE("chunk_coordinator serves waiting requests in the order they arrived", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 20, cfg);

    auto const c0 = coord.claim_chunk(t0);
    auto const c1 = coord.claim_chunk(t0);
    REQUIRE(c0);
    REQUIRE(c1);

    worker first, second;
    coord.request_chunk(first.reply, t0);
    coord.request_chunk(second.reply, t0);
    CHECK(coord.waiting() == 2);

    coord.chunk_failed(*c1, t0);
    auto const to_first = first.answer();
    REQUIRE(to_first);
    REQUIRE(to_first->has_value());
    CHECK((*to_first)->claim.chunk_id == 1u);
    CHECK_FALSE(second.answer().has_value());

    coord.chunk_failed(*c0, t0);
    auto const to_second = second.answer();
    REQUIRE(to_second);
    REQUIRE(to_second->has_value());
    CHECK((*to_second)->claim.chunk_id == 0u);
}

TEST_CASE("chunk_coordinator tells waiting and later requests when the range has no more work", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 10, cfg);

    worker holder, idle, late;
    coord.request_chunk(holder.reply, t0);
    auto const held = holder.answer();
    REQUIRE(held);
    REQUIRE(held->has_value());
    coord.request_chunk(idle.reply, t0);
    CHECK_FALSE(idle.answer().has_value());

    coord.chunk_completed((*held)->claim);

    // The waiting one is answered: there is nothing left for it.
    auto const dismissed = idle.answer();
    REQUIRE(dismissed);
    CHECK_FALSE(dismissed->has_value());
    CHECK(coord.waiting() == 0);

    // And so is anyone who asks afterwards.
    coord.request_chunk(late.reply, t0);
    auto const refused = late.answer();
    REQUIRE(refused);
    CHECK_FALSE(refused->has_value());
}

TEST_CASE("chunk_coordinator dismisses its waiting requests when the range is stopped", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 10, cfg);

    auto const held = coord.claim_chunk(t0);
    REQUIRE(held);
    worker a, b;
    coord.request_chunk(a.reply, t0);
    coord.request_chunk(b.reply, t0);
    REQUIRE(coord.waiting() == 2);

    coord.dismiss_waiting();

    for (auto* w : {&a, &b}) {
        auto const answer = w->answer();
        REQUIRE(answer);
        CHECK_FALSE(answer->has_value());
    }
    CHECK(coord.waiting() == 0);
}

TEST_CASE("chunk_coordinator keeps a chunk free when its worker cannot be answered", "[chunk_coordinator]") {
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(1, 10, cfg);

    auto const held = coord.claim_chunk(t0);
    REQUIRE(held);

    // The first waiting worker goes away; the second is still there.
    worker gone, present;
    coord.request_chunk(gone.reply, t0);
    coord.request_chunk(present.reply, t0);
    gone.reply->close();

    coord.chunk_failed(*held, t0);

    // The chunk was not left with the worker that is gone.
    auto const answered = present.answer();
    REQUIRE(answered);
    REQUIRE(answered->has_value());
    CHECK((*answered)->claim.chunk_id == 0u);
    CHECK(coord.get_progress().chunks_in_progress == 1);
    CHECK(coord.waiting() == 0);
}

// =============================================================================
// Many workers
// =============================================================================

TEST_CASE("chunk_coordinator hands each chunk to one of many workers", "[chunk_coordinator]") {
    constexpr uint32_t chunk_count = 4000;
    constexpr size_t worker_count = 8;

    // One block per chunk; nothing stalls, so no chunk is handed out twice.
    chunk_coordinator_config cfg{.chunk_size = 1, .stall_timeout_secs = 3600};
    chunk_coordinator coord(1, chunk_count, cfg);

    std::vector<worker> workers(worker_count);
    std::vector<uint32_t> handed(chunk_count, 0);
    for (auto& w : workers) coord.request_chunk(w.reply, t0);

    size_t leaving = 0;
    while (leaving < worker_count) {
        leaving = 0;
        for (auto& w : workers) {
            auto const answer = w.answer();
            if ( ! answer) {
                // Waiting: every chunk is held by someone else.
                continue;
            }
            if ( ! answer->has_value()) {
                ++leaving;
                continue;
            }
            ++handed[(*answer)->claim.chunk_id];
            // This chunk is still held, so the range is not done.
            REQUIRE_FALSE(coord.is_complete());
            coord.chunk_completed((*answer)->claim);
            coord.request_chunk(w.reply, t0);
        }
        // Workers told to leave are not asked again; count the ones that were.
        if (coord.is_complete()) {
            leaving = worker_count;
        }
    }

    CHECK(coord.is_complete());
    CHECK(coord.get_progress().chunks_completed == chunk_count);
    CHECK(coord.get_progress().chunks_in_progress == 0);
    CHECK(coord.waiting() == 0);
    size_t handed_once = 0;
    for (auto const count : handed) {
        if (count == 1) ++handed_once;
    }
    CHECK(handed_once == chunk_count);
}

TEST_CASE("chunk_coordinator completes every chunk once while workers fail and stall", "[chunk_coordinator]") {
    constexpr uint32_t chunk_count = 3000;
    constexpr size_t worker_count = 8;

    chunk_coordinator_config cfg{.chunk_size = 1, .stall_timeout_secs = 1};
    chunk_coordinator coord(1, chunk_count, cfg);

    // Each worker holds at most one assignment, and may keep it for several
    // steps: long enough for its request to stall and its chunk to be handed
    // to another worker while it still works on it.
    struct state {
        worker channel;
        std::optional<chunk_assignment> held;
        bool downloaded{false};
        bool asked{false};
        bool left{false};
    };
    std::vector<state> workers(worker_count);
    std::vector<uint32_t> deliveries(chunk_count, 0);

    uint64_t random = 88172645463325252ULL;
    auto const next = [&] {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        return random;
    };

    auto now = t0;
    size_t steps = 0;
    while ( ! coord.is_complete()) {
        REQUIRE(++steps < 10'000'000);
        now += 100ms;
        coord.check_timeouts(now);

        auto& w = workers[next() % worker_count];
        if (w.left) continue;
        if ( ! w.held) {
            if ( ! w.asked) {
                coord.request_chunk(w.channel.reply, now);
                w.asked = true;
            }
            auto const answer = w.channel.answer();
            if ( ! answer) continue;        // still waiting
            w.asked = false;
            if ( ! answer->has_value()) {
                w.left = true;
                continue;
            }
            w.held = **answer;
            w.downloaded = false;
            continue;
        }

        // One step in four the worker does nothing, so some requests stall.
        switch (next() % 4) {
            case 0:
                break;
            case 1:
                coord.chunk_failed(w.held->claim, now);
                w.held.reset();
                break;
            case 2:
                coord.chunk_downloaded(w.held->claim);
                w.downloaded = true;
                break;
            default:
                ++deliveries[w.held->claim.chunk_id];
                coord.chunk_completed(w.held->claim);
                w.held.reset();
                break;
        }
    }

    // Complete means every chunk was delivered at least once, and the count is
    // exact however many workers delivered the same chunk.
    size_t delivered = 0;
    size_t delivered_twice = 0;
    for (auto const count : deliveries) {
        if (count >= 1) ++delivered;
        if (count >= 2) ++delivered_twice;
    }
    CHECK(delivered == chunk_count);
    CHECK(delivered_twice > 0);     // the case did hand chunks on
    CHECK(coord.get_progress().chunks_completed == chunk_count);
    CHECK(coord.waiting() == 0);
    CHECK_FALSE(coord.claim_chunk(now).has_value());

    // Whoever asks now is told to leave. A worker may still have an answer it
    // never read: an assignment of a chunk that another worker, holding an
    // older claim of it, delivered in the meantime.
    for (auto& w : workers) {
        if (w.left) continue;
        (void)w.channel.answer();
        coord.request_chunk(w.channel.reply, now);
        auto const answer = w.channel.answer();
        REQUIRE(answer);
        CHECK_FALSE(answer->has_value());
    }
}
