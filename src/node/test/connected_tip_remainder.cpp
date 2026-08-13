// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <string>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// The trailing remainder, in the shape that produced the mainnet livelock (#653)
// =============================================================================
//
// A mainnet node reached 962946 of 963887 and stopped there for over an hour,
// with no error: 941 blocks downloaded and validated that never entered the UTXO
// set. The builder drains a remainder shorter than one batch only when NOT
// stale, and staleness was answered from the CONNECTED tip — the very height the
// remainder would advance. Stale kept the remainder unbuilt, unbuilt kept the
// connected tip 941 blocks back, and a connected tip six days old kept the
// answer stale.
//
// Why no test caught it: this harness mines blocks whose timestamps are recent,
// so the connected tip always looked current and the broken input never showed.
// The case below is built the other way round on purpose — a connected chain
// whose blocks are genuinely old, under a header tip that is current — which is
// what a node partway through a real sync actually looks like.
//
// Recency now comes from the validated header tip, which nothing the builder
// does can move.

namespace {

constexpr uint32_t block_spacing = 600;

// A run of blocks whose timestamps are DAYS old, then a run that is current.
// The old ones stand for what a sync has connected so far; the recent ones for
// the headers the node already has from the network.
struct aged_chain {
    std::vector<domain::chain::block> old_blocks;
    std::vector<domain::chain::block> recent_blocks;
};

aged_chain make_aged_chain(domain::chain::block const& genesis,
                           uint32_t old_len, uint32_t recent_len) {
    aged_chain out;
    auto prev = genesis.hash();
    uint32_t height = 1;

    // Six days back, which is what 941 blocks of spacing comes to — comfortably
    // past the 24 hour staleness limit.
    auto const old_base = uint32_t(zulu_time()) - (6 * 24 * 3600);
    for (uint32_t i = 0; i < old_len; ++i) {
        out.old_blocks.push_back(
            mine_block(prev, height, old_base + i * block_spacing, 0, {}, 0));
        prev = out.old_blocks.back().hash();
        ++height;
    }

    auto const recent_base = uint32_t(zulu_time()) - (recent_len + 2) * block_spacing;
    for (uint32_t i = 0; i < recent_len; ++i) {
        out.recent_blocks.push_back(
            mine_block(prev, height, recent_base + i * block_spacing, 0, {}, 0));
        prev = out.recent_blocks.back().hash();
        ++height;
    }
    return out;
}

} // namespace

TEST_CASE("a remainder shorter than one batch is built while the node runs",
          "[node][connected_tip][remainder]") {
    // THE DISCRIMINATING CONTROL. The connected chain is six days old and the
    // header tip is current — the mainnet shape — and the remainder is far
    // shorter than the 1000-block batch, so it is drained only if the node is
    // judged current. No stop, no restart: the run that connects them is the run
    // that must finish them.
    chain_fixture fixture("remainder_live");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const chain_blocks = make_aged_chain(domain::chain::block::genesis_regtest(), 4, 3);

    // Every header the node has, old and recent alike: this is the header sync
    // having run ahead of the build, which is what puts a current tip above an
    // old connected chain.
    std::vector<domain::chain::block> all;
    all.insert(all.end(), chain_blocks.old_blocks.begin(), chain_blocks.old_blocks.end());
    all.insert(all.end(), chain_blocks.recent_blocks.begin(), chain_blocks.recent_blocks.end());
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    // The header tip is current, so the node is NOT behind the network — even
    // though nothing has been connected yet and the connected marker is at 0.
    CHECK_FALSE(chain.is_stale());

    // Connect only the old run: 4 blocks, a remainder far below batch_size.
    connect_bodies(fixture, chain_blocks.old_blocks, 1);

    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 4u);   // drained, rather than parked one batch short

    // And both markers describe the same block, which is the invariant the
    // publish is responsible for.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 4u);
    CHECK(heights->block == *built);
}

TEST_CASE("staleness does not come from the height the builder is trying to reach",
          "[node][connected_tip][remainder]") {
    // The negation of the case above, stated as a property rather than a run:
    // with a current header tip, the connected marker cannot make the node stale
    // no matter how far behind it is. Reading staleness from the connected tip
    // is what closes the loop, so this is the assertion that would go red if it
    // ever went back to doing that.
    chain_fixture fixture("remainder_not_connected_tip");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const chain_blocks = make_aged_chain(domain::chain::block::genesis_regtest(), 4, 3);
    std::vector<domain::chain::block> all;
    all.insert(all.end(), chain_blocks.old_blocks.begin(), chain_blocks.old_blocks.end());
    all.insert(all.end(), chain_blocks.recent_blocks.begin(), chain_blocks.recent_blocks.end());
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    // The connected marker pinned at genesis — as far behind as it can be.
    REQUIRE(chain.set_last_block_height(0) == database::result_code::success);
    CHECK_FALSE(chain.is_stale());

    // And pinned at the old connected tip, whose header is six days old: still
    // not stale, because that is not where the answer comes from.
    REQUIRE(chain.set_last_block_height(4) == database::result_code::success);
    CHECK_FALSE(chain.is_stale());
}

TEST_CASE("storing blocks does not advance the connected tip",
          "[node][connected_tip][remainder]") {
    // Storing makes a block downloadable, not connected: the bytes are in a
    // stdio buffer, the index entry is in memory, and no barrier has run. The
    // storage task used to write that height into the connected marker — and
    // only on the way out — which is how a stopped node came to claim a
    // connected tip 952 blocks beyond its own UTXO set.
    chain_fixture fixture("remainder_store_only");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const chain_blocks = make_aged_chain(domain::chain::block::genesis_regtest(), 4, 0);
    REQUIRE(fixture.organizer().add_headers(headers_of(chain_blocks.old_blocks)).headers_added
            == chain_blocks.old_blocks.size());
    persist_headers(fixture, chain_blocks.old_blocks, 1);

    auto const before = chain.get_last_heights();
    REQUIRE(before);

    // The STORAGE TASK, on its own — no build task behind it. Driving
    // store_chunk() directly would only test the API; what has to be pinned is
    // that the task which stores does not publish a connected tip.
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& blk : chain_blocks.old_blocks) {
        light.push_back(to_light(blk));
    }

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 256);
    std::atomic<uint32_t> contiguous{1};

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = 1,
        .chunk_id = 0,
        .blocks = std::move(light),
        .source_peer = nullptr,
        .generation = chain.headers().generation()
    }));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ctx.run_for(std::chrono::seconds(60));
    while (output.try_receive([](std::error_code, chunk_validated) {})) {}

    // The blocks really were stored, so the assertion below is about a task that
    // did its work rather than one that did nothing.
    CHECK(contiguous.load() > 1u);

    auto const after = chain.get_last_heights();
    REQUIRE(after);
    CHECK(after->block == before->block);   // unmoved by storage alone
}
