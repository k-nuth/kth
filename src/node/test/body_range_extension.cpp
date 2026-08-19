// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <kth/node/detail/body_range.hpp>

using namespace kth;
using namespace kth::node::sync;

// =============================================================================
// The body range must follow a header tip that moves
// =============================================================================
//
// A mainnet IBD ended with seven bodies missing. The bodies had reached 964759,
// the header tip then moved to 964766, and nothing ever asked for 964760-964766:
// the coordinator for the range ending at 964759 was complete, every worker that
// started exited with nothing to claim, and the run finished with those seven
// heights carrying a header and `file=-1`.
//
// The state that went stale was a bool -- "the post-checkpoint range was sent" --
// which cannot record WHICH range was sent. The end height can, and these are the
// cases it has to get right.

namespace {

constexpr uint32_t checkpoint = 951146;

// The heights from the run, so the case that was actually observed is a value
// here rather than a story in a comment.
constexpr uint32_t observed_bodies = 964759;
constexpr uint32_t observed_headers = 964766;

} // namespace

TEST_CASE("body range - the run that lost seven bodies opens them", "[node][sync][body_range]") {
    // Bodies drained the range that ended at 964759; the tip is seven ahead.
    auto const range = next_slow_sync_range(
        observed_bodies, observed_headers, checkpoint, observed_bodies);

    REQUIRE(range);
    CHECK(range->start == 964760);
    CHECK(range->end == 964766);
    CHECK(range->end - range->start + 1 == 7);
}

TEST_CASE("body range - a tip that has not moved is owed nothing", "[node][sync][body_range]") {
    CHECK_FALSE(next_slow_sync_range(964759, 964759, checkpoint, 964759));
}

TEST_CASE("body range - bodies past the tip are owed nothing", "[node][sync][body_range]") {
    // Not reachable from a healthy sync, and the answer must still be "nothing"
    // rather than a range that runs backwards.
    CHECK_FALSE(next_slow_sync_range(964766, 964759, checkpoint, 964759));
}

TEST_CASE("body range - the first post-checkpoint range needs no previous one",
    "[node][sync][body_range]") {

    auto const range = next_slow_sync_range(checkpoint, 964759, checkpoint, std::nullopt);

    REQUIRE(range);
    CHECK(range->start == checkpoint + 1);
    CHECK(range->end == 964759);
}

TEST_CASE("body range - below the checkpoint the FAST range still owns the heights",
    "[node][sync][body_range]") {

    // Two coordinators over the same blocks is the thing this refusal prevents.
    CHECK_FALSE(next_slow_sync_range(checkpoint - 1, 964759, checkpoint, std::nullopt));
}

// -----------------------------------------------------------------------------
// A range in flight is not replaced
// -----------------------------------------------------------------------------

TEST_CASE("body range - a tip that moves while the previous range is in flight waits",
    "[node][sync][body_range]") {

    // Bodies are at 960000 inside a range that runs to 964759; the tip moves to
    // 964766. Opening now would stop that coordinator and drop the chunks its
    // workers hold, to be claimed again from a range starting at 960001.
    CHECK_FALSE(next_slow_sync_range(960000, 964766, checkpoint, 964759));
}

TEST_CASE("body range - successive advances while in flight stay quiet",
    "[node][sync][body_range]") {

    for (auto const tip : {964760u, 964763u, 964766u, 964800u}) {
        CAPTURE(tip);
        CHECK_FALSE(next_slow_sync_range(960000, tip, checkpoint, 964759));
    }
}

TEST_CASE("body range - the remainder is opened once the previous range drains",
    "[node][sync][body_range]") {

    // Same tip as the case above, one event later: the bodies reached the end of
    // the range, so the event that drained it is the one that opens the rest.
    auto const range = next_slow_sync_range(964759, 964800, checkpoint, 964759);

    REQUIRE(range);
    CHECK(*range == slow_sync_range{964760, 964800});
}

TEST_CASE("body range - a duplicate event at the same height decides the same thing",
    "[node][sync][body_range]") {

    // Several peers announcing one tip produce several validated batches ending
    // at the same height. The decision is a function of the heights, so the
    // second one cannot open a second range: after the first, `range_end` is
    // 964766 and the bodies have not moved.
    auto const first = next_slow_sync_range(964759, 964766, checkpoint, 964759);
    REQUIRE(first);

    auto const second = next_slow_sync_range(964759, 964766, checkpoint, first->end);
    CHECK_FALSE(second);
}

// -----------------------------------------------------------------------------
// Reorg
// -----------------------------------------------------------------------------

TEST_CASE("body range - a rewound tip is owed the range above the fork",
    "[node][sync][body_range]") {

    // A reorg rewinds the bodies to the fork and the headers describe the new
    // branch above it. The range recorded before the switch ended higher than
    // the bodies now are; read as "in flight" it would refuse to refill the
    // branch, which is the stall the reorg path records its own range to avoid.
    auto const range = next_slow_sync_range(964700, 964766, checkpoint, 964700);

    REQUIRE(range);
    CHECK(*range == slow_sync_range{964701, 964766});
}

TEST_CASE("body range - a stale end from before a reorg holds the refill back",
    "[node][sync][body_range]") {

    // The negation of the case above, as a value: this is what the coordinator
    // would decide if the reorg did NOT record the range it opened.
    CHECK_FALSE(next_slow_sync_range(964700, 964766, checkpoint, 964759));
}

TEST_CASE("body range - a rewind drops an end that describes the old branch",
    "[node][sync][body_range]") {

    // The recorded end was 964759 on the branch the switch abandoned; the bodies
    // are now at 964700. Kept, it reads as a range in flight over blocks that are
    // no longer on the chain, and the branch above the fork is never refilled.
    CHECK_FALSE(range_end_after_rewind(964759, 964700));
}

TEST_CASE("body range - a rewind keeps an end the bodies still hold",
    "[node][sync][body_range]") {

    // At or below the rewound tip the range was completed over blocks the switch
    // did not take away. Dropping it would reopen heights already downloaded.
    CHECK(range_end_after_rewind(964700, 964700) == std::optional<uint32_t>{964700});
    CHECK(range_end_after_rewind(964600, 964700) == std::optional<uint32_t>{964600});
}

TEST_CASE("body range - a rewind with nothing recorded stays that way",
    "[node][sync][body_range]") {

    CHECK_FALSE(range_end_after_rewind(std::nullopt, 964700));
}

TEST_CASE("body range - a dropped end lets the refill through, a kept one does not",
    "[node][sync][body_range]") {

    // The two halves joined, which is the property the rewind exists for: what
    // the coordinator decides after a reorg, with and without the drop.
    constexpr uint32_t rewound = 964700;
    constexpr uint32_t tip = 964766;
    constexpr uint32_t stale = 964759;

    auto const dropped = range_end_after_rewind(stale, rewound);
    auto const refill = next_slow_sync_range(rewound, tip, checkpoint, dropped);
    REQUIRE(refill);
    CHECK(*refill == slow_sync_range{rewound + 1, tip});

    // And this is what it would decide if the end were carried over instead.
    CHECK_FALSE(next_slow_sync_range(rewound, tip, checkpoint, stale));
}

// -----------------------------------------------------------------------------
// The ends of the number line
// -----------------------------------------------------------------------------

TEST_CASE("body range - a tip at UINT32_MAX is opened without wrapping",
    "[node][sync][body_range]") {

    constexpr auto top = std::numeric_limits<uint32_t>::max();

    auto const range = next_slow_sync_range(top - 1, top, checkpoint, top - 1);

    REQUIRE(range);
    CHECK(range->start == top);
    CHECK(range->end == top);
}

TEST_CASE("body range - bodies at UINT32_MAX are owed nothing", "[node][sync][body_range]") {
    constexpr auto top = std::numeric_limits<uint32_t>::max();

    // The `+ 1` that would wrap is unreachable: it lives below the check that
    // the tip is strictly above the bodies, and nothing is strictly above the
    // largest height there is.
    CHECK_FALSE(next_slow_sync_range(top, top, checkpoint, top));
    CHECK_FALSE(next_slow_sync_range(top, top - 1, checkpoint, top - 1));
}

TEST_CASE("body range - a checkpoint at zero admits the range from genesis",
    "[node][sync][body_range]") {

    auto const range = next_slow_sync_range(0, 7, 0, std::nullopt);

    REQUIRE(range);
    CHECK(*range == slow_sync_range{1, 7});
}

TEST_CASE("body range - the reason and the range are one rule read twice",
    "[node][sync][body_range]") {

    // The log names a reason and the coordinator acts on a range, and the two
    // must never disagree: a reason derived separately would keep reporting a
    // cause that is no longer the one that decided. So a range is owed exactly
    // when there is no reason not to, over every case these controls cover.
    struct sample {
        uint32_t bodies;
        uint32_t headers;
        std::optional<uint32_t> end;
    };

    constexpr auto top = std::numeric_limits<uint32_t>::max();

    std::vector<sample> const samples{
        {964759, 964766, 964759},        // owed: the run this comes from
        {964759, 964759, 964759},        // level
        {964766, 964759, 964759},        // bodies past the tip
        {960000, 964766, 964759},        // in flight
        {964759, 964800, 964759},        // drained, remainder owed
        {checkpoint, 964759, std::nullopt},
        {checkpoint - 1, 964759, std::nullopt},   // the FAST range owns these
        {964700, 964766, 964759},        // a stale end from before a reorg
        {964700, 964766, 964700},        // the same, after the rewind dropped it
        {top - 1, top, top - 1},
        {top, top, top}
    };

    for (auto const& s : samples) {
        CAPTURE(s.bodies, s.headers, s.end ? int64_t(*s.end) : -1);
        auto const reason = why_no_range(s.bodies, s.headers, checkpoint, s.end);
        auto const range = next_slow_sync_range(s.bodies, s.headers, checkpoint, s.end);
        CHECK(range.has_value() == (reason == range_quiet::none));
    }
}

TEST_CASE("body range - each refusal names the reason it refused for",
    "[node][sync][body_range]") {

    // Not just "some reason": the log says which, so each condition has to be
    // the one reported rather than whichever is checked first by accident.
    CHECK(why_no_range(964759, 964759, checkpoint, 964759) == range_quiet::no_advance);
    CHECK(why_no_range(checkpoint - 1, 964759, checkpoint, std::nullopt)
        == range_quiet::below_checkpoint);
    CHECK(why_no_range(960000, 964766, checkpoint, 964759) == range_quiet::in_flight);
    CHECK(why_no_range(964759, 964766, checkpoint, 964759) == range_quiet::none);

    // Two refusals at once, which is what actually pins the order: the bodies are
    // below the checkpoint AND behind a recorded end. The FAST range owning these
    // heights is the reason, because it is the one that decides — a log naming
    // the recorded range instead would send someone looking at a coordinator that
    // has nothing to do with it.
    CHECK(why_no_range(checkpoint - 10, 964766, checkpoint, checkpoint - 5)
        == range_quiet::below_checkpoint);

    // And the first condition wins over both: nothing owed is nothing owed,
    // whatever else is true underneath.
    CHECK(why_no_range(checkpoint - 10, checkpoint - 10, checkpoint, checkpoint - 5)
        == range_quiet::no_advance);
}

TEST_CASE("body range - every trigger has a name of its own", "[node][sync][body_range]") {
    // The log line that reconstructs a decision names the door it came through,
    // and two doors sharing a name would make that line unreadable.
    std::vector<std::string> names{
        to_string(body_range_trigger::headers_advanced),
        to_string(body_range_trigger::header_sync_complete),
        to_string(body_range_trigger::block_validated),
        to_string(body_range_trigger::chunk_validated),
        to_string(body_range_trigger::utxo_build_advanced),
        to_string(body_range_trigger::reorg)
    };

    auto sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    for (auto const& name : names) {
        CAPTURE(name);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
    }
}
