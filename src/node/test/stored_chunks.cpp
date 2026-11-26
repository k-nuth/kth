// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>

#include <kth/node/detail/stored_chunks.hpp>
#include <kth/node/sync/messages.hpp>

using namespace kth;
using namespace kth::node::detail;
using namespace kth::node::sync;

// =============================================================================
// The height the bodies are synced to moves only through chunks of this chain
// =============================================================================

namespace {

chunk_validated stored_at(uint32_t start_height, uint32_t block_count, uint64_t generation) {
    return chunk_validated{
        .start_height = start_height,
        .block_count = block_count,
        .result = error::success,
        .source_peer = nullptr,
        .generation = generation};
}

} // namespace

TEST_CASE("stored chunks: the height moves through consecutive chunks, in whatever order they arrive",
          "[stored_chunks]") {
    constexpr uint64_t generation = 3;
    stored_chunks chunks;

    // 21..30 arrives before 11..20: nothing is consecutive yet.
    CHECK(chunks.stored(stored_at(21, 10, generation), generation, 10) == 10u);
    CHECK(chunks.size() == 1u);

    // 11..20 closes the gap, and both are counted.
    CHECK(chunks.stored(stored_at(11, 10, generation), generation, 10) == 30u);
    CHECK(chunks.size() == 0u);

    CHECK(chunks.stored(stored_at(31, 1, generation), generation, 30) == 31u);
}

TEST_CASE("stored chunks: a chunk stored for a branch the chain has left moves nothing",
          "[stored_chunks]") {
    // The chain switched at height 10 and the generation moved from 3 to 4.
    // The report of a chunk stored at 11 before the switch arrives after it:
    // on the new branch, 11 is a block this node does not hold.
    stored_chunks chunks;

    CHECK(chunks.stored(stored_at(11, 1, 3), 4, 10) == 10u);
    CHECK(chunks.size() == 0u);

    // The new branch's own chunk at 11 is counted, and only that one: the
    // stale report left nothing behind for 12 to continue from.
    CHECK(chunks.stored(stored_at(11, 1, 4), 4, 10) == 11u);
    CHECK(chunks.stored(stored_at(13, 1, 4), 4, 11) == 11u);
    CHECK(chunks.size() == 1u);
}

TEST_CASE("stored chunks: after a rewind, what waited above the new height is forgotten",
          "[stored_chunks]") {
    constexpr uint64_t before = 3;
    constexpr uint64_t after = 4;
    stored_chunks chunks;

    // 13..14 and 16 wait for 11..12 and 15.
    CHECK(chunks.stored(stored_at(13, 2, before), before, 10) == 10u);
    CHECK(chunks.stored(stored_at(16, 1, before), before, 10) == 10u);
    REQUIRE(chunks.size() == 2u);

    SECTION("a chunk that ends at the new height stays") {
        chunks.forget_above(14);
        CHECK(chunks.size() == 1u);
    }

    SECTION("a chunk that reaches above the new height goes, whole") {
        chunks.forget_above(13);
        CHECK(chunks.size() == 0u);
    }

    SECTION("the new branch is then counted from the rewound height alone") {
        chunks.forget_above(10);
        REQUIRE(chunks.size() == 0u);
        CHECK(chunks.stored(stored_at(11, 2, after), after, 10) == 12u);
    }
}

// =============================================================================
// An outcome from a branch the chain has left is nobody's, success or failure
// =============================================================================

TEST_CASE("chunk outcome: a peer's chunk for a branch the chain has left is recognised, whatever it reports",
          "[stored_chunks]") {
    constexpr uint64_t before_switch = 3;
    constexpr uint64_t after_switch = 4;

    auto failed = stored_at(11, 10, before_switch);
    failed.result = error::merkle_mismatch;

    CHECK(stored_at(11, 10, before_switch).peer_outcome_from_abandoned_branch(after_switch));
    CHECK(failed.peer_outcome_from_abandoned_branch(after_switch));

    // On the branch it was downloaded for, either one is the chain's business.
    CHECK_FALSE(stored_at(11, 10, before_switch).peer_outcome_from_abandoned_branch(before_switch));
    CHECK_FALSE(failed.peer_outcome_from_abandoned_branch(before_switch));
}

TEST_CASE("chunk outcome: a local body's outcome is matched to its submission, not to a generation",
          "[stored_chunks]") {
    auto local = stored_at(11, 1, 3);
    local.submission = 21;
    CHECK_FALSE(local.peer_outcome_from_abandoned_branch(4));
}
