// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::node::sync;

namespace {

// A chunk carrying one light_block at `height`, stamped with `generation`.
downloaded_chunk make_chunk(uint32_t height, uint64_t generation) {
    auto blk = std::make_shared<domain::chain::light_block const>(
        domain::chain::header{}, data_chunk{}, std::vector<uint32_t>{});

    std::vector<std::shared_ptr<domain::chain::light_block const>> blocks;
    blocks.push_back(std::move(blk));

    return downloaded_chunk{
        .start_height = height,
        .chunk_id = 0,
        .blocks = std::move(blocks),
        .source_peer = nullptr,
        .generation = generation
    };
}

} // namespace

// =============================================================================
// Stale work is dropped, not stored
// =============================================================================
//
// This is the consumer that justifies the generation stamp. A chunk downloaded
// for a branch the node has since abandoned must never reach storage: its
// heights name different blocks on the new chain, so storing it would write the
// wrong data and mark the wrong hashes have_data.
//
// Both races this guards against are invisible to a test that only checks the
// counter's value, which is why this drives the real task over real channels.

TEST_CASE("block_storage_task drops a chunk from an earlier generation", "[reorg][drain]") {
    test::chain_fixture fixture("drop");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Model the real sequence: the chunk is downloaded under the generation
    // current at that moment, and only afterwards does a reorg move the chain
    // onto another branch. Sending a chunk with a *higher* generation would
    // trip the same inequality without reproducing what actually happens.
    auto const generation_at_download = chain.headers().generation();
    auto stale = make_chunk(1, generation_at_download);

    // Two siblings on genesis, then switch onto the second: a branch change,
    // which is what bumps the generation.
    auto& index = chain.headers();
    auto const genesis = index.active_at(0);
    REQUIRE(genesis != blockchain::header_index::null_index);
    auto const genesis_hash = index.get_hash(genesis);

    auto const add_child = [&](uint32_t nonce) {
        domain::chain::header hdr{1, genesis_hash, null_hash, 1000 + nonce, 0x207fffff, nonce};
        auto const hash = domain::chain::hash(hdr);
        auto const r = index.add(hash, hdr);
        REQUIRE(r.inserted);
        return r.index;
    };
    auto const branch_a = add_child(1);
    index.active_set_tip(branch_a);
    auto const branch_b = add_child(2);
    index.active_set_tip(branch_b);

    REQUIRE(index.generation() > generation_at_download);

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 16);
    std::atomic<uint32_t> contiguous{1};

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::detached);

    // The chunk was stamped before the switch, so it is now stale.
    REQUIRE(input.try_send(std::error_code{}, std::move(stale)));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));

    ctx.run_for(std::chrono::seconds(5));

    // Dropped, not stored: height 1 on the new branch never gained block data,
    // and the contiguous cursor did not advance.
    CHECK_FALSE(index.has_block_data(index.active_at(1)));
    CHECK(contiguous.load() == 1);
}

TEST_CASE("a chunk from the current generation is not dropped", "[reorg][drain]") {
    // The counterpart: the drop must be selective. A chunk stamped with the
    // current generation has to reach storage — a filter that rejected
    // everything would pass the test above while stalling sync completely.
    test::chain_fixture fixture("keep");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 16);
    std::atomic<uint32_t> contiguous{1};

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, make_chunk(1, chain.headers().generation())));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));

    ctx.run_for(std::chrono::seconds(5));

    // It was NOT dropped: storage attempted it and reported the outcome
    // downstream. (The store itself fails here because the fixture has no header
    // at height 1 — what matters is that the chunk was processed rather than
    // discarded before reaching storage.)
    bool reached_storage = false;
    output.try_receive([&](std::error_code, chunk_validated) { reached_storage = true; });
    CHECK(reached_storage);
}

// =============================================================================
// The download-side half of the protocol
// =============================================================================

TEST_CASE("the coordinator reports a generation that moves with the branch", "[reorg][drain]") {
    // block_download_task captures the generation before reading any hash and
    // re-checks it once the request is assembled, so what it depends on is that
    // the counter changes exactly when the branch does — no sooner (which would
    // discard good work) and no later (which would let stale work pass).
    //
    // Scope, stated so this is not read as more than it is: this pins that
    // contract. It does NOT reproduce the interleaving inside the downloader —
    // that needs a peer mid-download — so the capture-before-read ordering is
    // covered by inspection and by the storage drop test above.
    test::chain_fixture fixture("coordinator_gen");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto& index = fixture.chain().headers();
    auto const genesis = index.active_at(0);
    REQUIRE(genesis != blockchain::header_index::null_index);
    auto const genesis_hash = index.get_hash(genesis);

    auto const add_child = [&](uint32_t nonce) {
        domain::chain::header hdr{1, genesis_hash, null_hash, 1000 + nonce, 0x207fffff, nonce};
        auto const r = index.add(domain::chain::hash(hdr), hdr);
        REQUIRE(r.inserted);
        return r.index;
    };

    auto const before_extension = index.generation();

    // Extending the chain is not a branch change: work in flight for these
    // heights is still valid, so the generation must hold. If it moved here the
    // download task would discard good chunks on every new tip.
    auto const branch_a = add_child(1);
    index.active_set_tip(branch_a);
    CHECK(index.generation() == before_extension);

    // Moving onto a sibling IS a branch change: height 1 now names a different
    // block, so anything downloaded for it must become recognisable as stale.
    auto const branch_b = add_child(2);
    index.active_set_tip(branch_b);
    CHECK(index.generation() == before_extension + 1);

    // And the chain visible at that new generation is the new branch, not the
    // one that was just abandoned.
    CHECK(index.active_at(1) == branch_b);
    CHECK(index.active_at(1) != branch_a);
}
