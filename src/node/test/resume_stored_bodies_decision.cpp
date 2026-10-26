// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/sync/block_tasks.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

// =============================================================================
// Where the body sync resumes from: the bodies the store holds, contiguously
// above the connected tip, on the active chain
// =============================================================================
//
// The decision the orchestrator makes once at startup, over the real index of a
// real store. The integrated cases in resume_stored_bodies.cpp show the defect
// through the node; these pin the edges of the answer: a gap, a side branch, and
// a store with nothing above its connected tip.

namespace {

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

// Bytes into the store and have_data onto the index, through the chain's own
// store path, for exactly the blocks given at `start_height`.
void store_at(blockchain::block_chain& chain, std::vector<domain::chain::block> const& blocks,
              uint32_t start_height) {
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& blk : blocks) {
        light.push_back(to_light(blk));
    }
    ::asio::io_context ctx;
    auto stored = ::asio::co_spawn(ctx, chain.store_chunk(light, start_height), ::asio::use_future);
    ctx.run_for(std::chrono::seconds(30));
    REQUIRE(stored.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    REQUIRE( ! stored.get());
}

std::vector<domain::chain::block> slice(std::vector<domain::chain::block> const& all,
                                        uint32_t from, uint32_t to) {
    return {all.begin() + (from - 1), all.begin() + to};
}

} // namespace

TEST_CASE("stored_bodies_from: nothing stored above the connected tip answers the tip",
          "[node][sync][resume_bodies][decision]") {
    chain_fixture fixture("stored_bodies_none");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), 6);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    // Headers only.
    CHECK(stored_bodies_from(chain.headers(), 0) == 0u);
    CHECK(stored_bodies_from(chain.headers(), 3) == 3u);

    // Above the active tip there is nothing to count, and nothing to read past.
    CHECK(stored_bodies_from(chain.headers(), 6) == 6u);
    CHECK(stored_bodies_from(chain.headers(), 9) == 9u);
}

TEST_CASE("stored_bodies_from: contiguous bodies above the tip are counted to the last one",
          "[node][sync][resume_bodies][decision]") {
    chain_fixture fixture("stored_bodies_contiguous");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), 6);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    store_at(chain, slice(all, 4, 6), 4);

    CHECK(stored_bodies_from(chain.headers(), 3) == 6u);
    // From inside the stored run the answer is the same end.
    CHECK(stored_bodies_from(chain.headers(), 5) == 6u);
    // From below it, the missing block right above the tip stops the walk at once:
    // bodies further up are not reachable without the ones between.
    CHECK(stored_bodies_from(chain.headers(), 2) == 2u);
}

TEST_CASE("stored_bodies_from: a gap stops the count, and what is above it stays owed",
          "[node][sync][resume_bodies][decision]") {
    chain_fixture fixture("stored_bodies_gap");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), 8);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    store_at(chain, slice(all, 4, 5), 4);
    store_at(chain, slice(all, 7, 8), 7);

    CHECK(stored_bodies_from(chain.headers(), 3) == 5u);
}

TEST_CASE("stored_bodies_from: a body on a side branch is not a body at that height",
          "[node][sync][resume_bodies][decision]") {
    chain_fixture fixture("stored_bodies_side_branch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), 6);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    // A competing block 4 on top of the same 3, with less work behind it than
    // the trunk: a side branch the index holds and the active chain does not.
    // The organizer stores it and reports the batch as not advancing the tip
    // (`stale_chain`, nothing added), which is the contract for a side branch.
    auto const fork = mine_block(all[2].hash(), 4, all[3].header().timestamp() + 1, 7, {}, 0);
    auto const side = fixture.organizer().add_headers(headers_of({fork}));
    REQUIRE(side.headers_added == 0u);
    REQUIRE(side.error == error::stale_chain);
    auto const fork_idx = chain.headers().find(fork.hash());
    REQUIRE(fork_idx != blockchain::header_index::null_index);
    REQUIRE(chain.headers().active_at(4) != fork_idx);

    store_at(chain, {fork}, 4);
    REQUIRE(chain.headers().has_block_data(fork_idx));

    // The active block 4 has no data, whatever its rival holds.
    CHECK(stored_bodies_from(chain.headers(), 3) == 3u);

    // The trunk's own 4..6 stored: now they count, and the rival still does not matter.
    store_at(chain, slice(all, 4, 6), 4);
    CHECK(stored_bodies_from(chain.headers(), 3) == 6u);
}
