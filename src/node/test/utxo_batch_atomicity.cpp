// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// A batch that did not finish must not be resumed
// =============================================================================
//
// Applying a UTXO delta mutates the maps in place: there is no staging, no
// transaction and nothing to roll back. So a batch interrupted between its delta
// and its height marker leaves the set part-applied, and the marker still names
// the batch before it — resuming would reapply mutations that are already in.
//
// The in-flight marker is what makes that detectable. It is written before the
// first mutation and cleared only once the delta, the deferred deletions and the
// height have all landed, so finding it at startup means exactly one thing.

namespace {

// Drive the build task alone, with the marker already set, and capture what it
// reports. No blocks are needed: the check happens before any work.
std::vector<std::string> run_build_and_collect_fatals(chain_fixture& fixture,
                                                      uint32_t start_height) {
    std::vector<std::string> fatals;

    ::asio::io_context ctx;
    std::atomic<uint32_t> contiguous{start_height};

    ::asio::co_spawn(ctx,
        utxo_build_task(fixture.chain(), contiguous, start_height,
            domain::config::network::regtest,
            [] { return false; },
            [&fatals](std::string const& reason) { fatals.push_back(reason); }),
        ::asio::detached);

    ctx.run_for(std::chrono::seconds(2));
    return fatals;
}

} // namespace

TEST_CASE("a batch left in flight stops the build instead of resuming it", "[node][utxo][atomicity]") {
    chain_fixture fixture("batch_dirty");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Stands in for a previous run that began a batch and never finished it.
    REQUIRE(chain.set_utxo_batch_dirty(1) == database::result_code::success);

    auto const fatals = run_build_and_collect_fatals(fixture, 1);

    REQUIRE(fatals.size() == 1);
    CHECK(fatals.front() == "a UTXO batch was left half-applied and the set cannot be resumed");

    // And it stopped before doing anything: refusing is the whole point, so a
    // build that reported the condition and carried on would be worse than one
    // that never checked.
    auto const built = chain.get_utxo_built_height();
    CHECK(( ! built || *built == 0));
}

TEST_CASE("a clean marker lets the build start", "[node][utxo][atomicity]") {
    // The other direction, so the test above cannot pass by the task refusing to
    // start for some unrelated reason.
    chain_fixture fixture("batch_clean");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.clear_utxo_batch_dirty() == database::result_code::success);

    auto const fatals = run_build_and_collect_fatals(fixture, 1);
    CHECK(fatals.empty());
}

TEST_CASE("connecting a batch leaves no marker behind", "[node][utxo][atomicity]") {
    // The marker is only useful if the successful path clears it. One that was
    // never cleared would refuse every subsequent start, which is a different
    // failure and just as bad.
    chain_fixture fixture("batch_cycle");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;

    std::vector<domain::chain::block> blocks;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 3; ++h) {
        blocks.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = blocks.back().hash();
    }

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 3);
    persist_headers(fixture, blocks, 1);
    connect_bodies(fixture, blocks, 1);

    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    REQUIRE(*built == 3);

    auto const dirty = chain.get_utxo_batch_dirty();
    REQUIRE(dirty);
    CHECK_FALSE(*dirty);
}
