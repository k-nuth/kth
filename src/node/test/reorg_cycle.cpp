// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/reorg.hpp>

#include "../../blockchain/test/regtest_miner.hpp"
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::node::sync;

// =============================================================================
// The reorganization, end to end
// =============================================================================
//
// A trunk to height 100, a block at 101 that spends a coinbase matured on that
// trunk, and a competing branch of three blocks from 100 that outweighs it. The
// node connects the first chain, switches to the second, and connects that —
// through the same tasks the sync coordinator drives, against the same header
// index, flat-file store, undo files and UTXO-Z the node runs on.
//
// Every block here satisfies consensus (real proof of work, real merkle roots, a
// real signature over a real prevout), so the connect path runs full validation
// rather than the shortcuts a synthetic block would slip through.

namespace {

// Blocks are spaced two minutes apart and the chain ends near the present. The
// node treats an old tip as "still in IBD" and then batches UTXO work a thousand
// blocks at a time, so a chain stuck in 2011 would never build anything here.
constexpr uint32_t block_spacing = 120;

// Serialize a mined block the way a peer would send it and parse it back the way
// the node does, so what reaches storage arrived through the same code path as a
// block off the wire.
std::shared_ptr<domain::chain::light_block const> to_light(domain::chain::block const& blk) {
    data_chunk raw(blk.serialized_size());
    byte_writer writer(raw);
    auto const written = blk.to_data(writer);
    REQUIRE(written);

    byte_reader reader(raw);
    auto light = domain::chain::light_block::from_data(reader, true);
    REQUIRE(light);
    return std::make_shared<domain::chain::light_block const>(std::move(*light));
}

// Write the headers into the internal database, standing in for the node's
// header-persist task, which runs when a header sync completes. get_header(height)
// reads that table, and the UTXO build needs it twice over: to rebuild its
// median-time-past window, and to decide whether the chain is still far enough
// behind to batch a thousand blocks at a time. Without it the build sits idle.
//
// Deliberately NOT called after the switch below: rewriting the heights a reorg
// replaced is the reorg's own job, and doing it here would paper over whether it
// actually happens.
void persist_headers(test::chain_fixture& fixture, std::vector<domain::chain::block> const& blocks,
                     uint32_t start_height) {
    domain::chain::header::list batch;
    batch.reserve(blocks.size());
    for (auto const& blk : blocks) {
        batch.push_back(blk.header());
    }
    REQUIRE( ! fixture.chain().organize_headers_batch(batch, start_height));
}

domain::message::header::list headers_of(std::vector<domain::chain::block> const& blocks) {
    domain::message::header::list headers;
    headers.reserve(blocks.size());
    for (auto const& blk : blocks) {
        headers.push_back(blk.header());
    }
    return headers;
}

// Run the real storage and UTXO-build tasks over `blocks`, a contiguous run
// starting at `start_height` whose headers are already in the index and in the
// by-height table. This is the node's connect path: bodies through
// block_storage_task, then utxo_build_task reading them back off disk to
// validate them, apply the UTXO delta and write the undo records.
void connect_bodies(test::chain_fixture& fixture, std::vector<domain::chain::block> const& blocks,
                    uint32_t start_height) {

    auto& chain = fixture.chain();
    auto const end_height = start_height + uint32_t(blocks.size()) - 1;

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 256);
    std::atomic<uint32_t> contiguous{start_height};

    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    light.reserve(blocks.size());
    for (auto const& blk : blocks) {
        light.push_back(to_light(blk));
    }

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, start_height, fixture.organizer(), &contiguous),
        ::asio::detached);

    ::asio::co_spawn(ctx,
        utxo_build_task(chain, contiguous, start_height, domain::config::network::regtest,
            [&chain, end_height] {
                auto const built = chain.get_utxo_built_height();
                return built && *built >= end_height;
            }),
        ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = start_height,
        .chunk_id = 0,
        .blocks = std::move(light),
        .source_peer = nullptr,
        .generation = chain.headers().generation()
    }));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));

    ctx.run_for(std::chrono::seconds(120));

    // Fail here if the tasks stalled, rather than at a height assertion further
    // down that would point at the wrong thing.
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    REQUIRE(*built >= end_height);

    // Drain what the storage task reported, so nothing is left owning blocks.
    while (output.try_receive([](std::error_code, chunk_validated) {})) {}
}

// UTXO-Z answers a miss with "queued", not "absent": the lookup is deferred to a
// sweep. Concluding a UTXO is gone without draining that queue would report
// whatever the sweep had not reached yet.
bool utxo_present(blockchain::block_chain& chain, hash_digest const& txid, uint32_t index,
                  uint32_t at_height) {
    if (chain.get_utxo(domain::chain::output_point{txid, index}, at_height)) {
        return true;
    }
    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, index);
    auto const [found, missing] = chain.utxo_process_pending_lookups();
    return found.contains(key);
}

} // namespace

// =============================================================================
// The miner
// =============================================================================

TEST_CASE("the miner produces blocks that satisfy regtest consensus", "[reorg][cycle]") {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const b1 = test::mine_block(genesis.hash(), 1, genesis.header().timestamp() + 600, 0, {}, 0);

    CHECK(b1.header().is_valid_proof_of_work(b1.hash(), false));
    CHECK(b1.header().previous_block_hash() == genesis.hash());
    CHECK(b1.generate_merkle_root() == b1.header().merkle());
    REQUIRE(b1.transactions().size() == 1);
    CHECK(b1.transactions().front().is_coinbase());
    CHECK(b1.is_valid_coinbase_script(1));
}

// =============================================================================
// The full cycle
// =============================================================================

TEST_CASE("the node reorganizes onto a heavier branch and back to a consistent state", "[reorg][cycle]") {
    test::chain_fixture fixture("cycle");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto& index = chain.headers();

    constexpr uint32_t trunk_len = 100;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    // -------------------------------------------------------------------------
    // The trunk: heights 1..100, shared by both branches.
    // -------------------------------------------------------------------------
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }

    auto const trunk_result = fixture.organizer().add_headers(headers_of(trunk));
    REQUIRE(trunk_result.headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        REQUIRE(heights->block == trunk_len);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        REQUIRE(*built == trunk_len);
    }

    // -------------------------------------------------------------------------
    // Branch A: one block at 101 spending the coinbase of block 1, which matures
    // exactly here — 101 - 1 == 100 confirmations.
    // -------------------------------------------------------------------------
    auto const& matured_coinbase = trunk.front().transactions().front();
    auto const matured_txid = matured_coinbase.hash();
    constexpr uint64_t fee = 1000;

    auto const spend = test::spend_p2pkh(matured_coinbase, 0,
        matured_coinbase.outputs()[0].value() - fee, chain.chain_settings().enabled_flags());
    auto const spend_txid = spend.hash();

    auto const a101 = test::mine_block(trunk.back().hash(), 101,
        base_time + 101 * block_spacing, 1, {spend}, fee);
    std::vector<domain::chain::block> const branch_a{a101};

    auto const a_result = fixture.organizer().add_headers(headers_of(branch_a));
    REQUIRE(a_result.headers_added == 1);
    persist_headers(fixture, branch_a, 101);
    connect_bodies(fixture, branch_a, 101);

    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        REQUIRE(heights->block == 101);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        REQUIRE(*built == 101);
    }

    // The spend really happened: the coinbase it consumed is gone from the set and
    // the output it created is in it. Without this the reorg below would be undoing
    // nothing and every assertion after it would pass vacuously.
    CHECK_FALSE(utxo_present(chain, matured_txid, 0, 101));
    CHECK(utxo_present(chain, spend_txid, 0, 101));
    CHECK(utxo_present(chain, a101.transactions().front().hash(), 0, 101));

    // -------------------------------------------------------------------------
    // Branch B: three blocks from height 100, outweighing A's single one.
    // -------------------------------------------------------------------------
    std::vector<domain::chain::block> branch_b;
    prev = trunk.back().hash();
    for (uint32_t h = 101; h <= 103; ++h) {
        branch_b.push_back(test::mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = branch_b.back().hash();
    }

    auto const generation_before = index.generation();
    auto const b_result = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(b_result.reorg_candidate);
    REQUIRE(b_result.reorg_fork_height == int32_t(trunk_len));
    REQUIRE(b_result.reorg_branch_head == index.find(branch_b.back().hash()));

    // -------------------------------------------------------------------------
    // The switch, through the same call the coordinator makes. No task is running
    // here, so the barrier is satisfied at once — its own behaviour is covered by
    // the drain tests.
    // -------------------------------------------------------------------------
    reorg_outcome reorg;
    {
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(chain, fixture.organizer(), b_result.reorg_branch_head, trunk_len,
                [] { return false; }),
            [&reorg](std::exception_ptr, reorg_outcome result) {
                reorg = result;
            });
        switch_ctx.run_for(std::chrono::seconds(30));
    }
    auto const outcome = reorg.result;

    // Nothing about this switch leaves the node in a state it cannot continue
    // from: the header table was rewritten and agrees with the chain.
    CHECK_FALSE(reorg.fatal);
    REQUIRE(outcome.ok);
    REQUIRE(outcome.validated_tip);
    CHECK(*outcome.validated_tip == trunk_len);

    // A's block is disconnected: its coinbase is out of the set and the coinbase it
    // spent is back, at the height it was originally created — not at 101, which
    // would misdate its maturity and its median-time-past.
    CHECK_FALSE(utxo_present(chain, a101.transactions().front().hash(), 0, trunk_len));
    CHECK_FALSE(utxo_present(chain, spend_txid, 0, trunk_len));
    CHECK(utxo_present(chain, matured_txid, 0, trunk_len));

    // The by-height header table names B's blocks already — rewritten by the
    // switch itself, while the writers were still parked. Nothing else would do
    // it: the background persist covers new heights, not heights whose block
    // changed, so without this every reader that addresses blocks by height
    // (median time past, the staleness check, the RPC surface) would keep
    // answering from the branch the node just left.
    for (uint32_t h = 101; h <= 103; ++h) {
        auto const persisted = chain.get_header(h);
        REQUIRE(persisted);
        CHECK(domain::chain::hash(*persisted) == branch_b[h - 101].hash());
    }

    // And the displaced header is no longer reachable by hash through that table.
    // Leaving its hash -> height entry would answer a lookup for A101 with B101 —
    // a different header than the one asked for.
    CHECK_FALSE(chain.get_height(a101.hash()));

    // The chain is on B — all the way to 103, since the switch re-points the
    // active chain onto the whole branch and leaves 101..103 headers-only for
    // block download to refill. The generation moved with it, so work in flight
    // for A is recognisable as stale.
    CHECK(index.active_tip_height() == 103);
    CHECK(index.generation() > generation_before);
    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        CHECK(heights->block == trunk_len);
    }

    // -------------------------------------------------------------------------
    // Connect B's bodies, the way the coordinator re-drives download after a switch.
    // -------------------------------------------------------------------------
    connect_bodies(fixture, branch_b, 101);

    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        CHECK(heights->block == 103);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        CHECK(*built == 103);
    }

    // Heights now name B's blocks.
    for (uint32_t h = 101; h <= 103; ++h) {
        auto const idx = index.active_at(int32_t(h));
        REQUIRE(idx != blockchain::header_index::null_index);
        CHECK(index.get_hash(idx) == branch_b[h - 101].hash());
    }

    // The abandoned block is still on disk. Its bytes are what a later switch back
    // to A would have to replay, and nothing in the reorg path deletes them.
    auto const abandoned = index.find(a101.hash());
    REQUIRE(abandoned != blockchain::header_index::null_index);
    CHECK(index.has_block_data(abandoned));

    // Final UTXO state: B's coinbases are present, A left nothing behind, and the
    // coinbase A spent is unspent again.
    for (auto const& blk : branch_b) {
        CHECK(utxo_present(chain, blk.transactions().front().hash(), 0, 103));
    }
    CHECK_FALSE(utxo_present(chain, a101.transactions().front().hash(), 0, 103));
    CHECK_FALSE(utxo_present(chain, spend_txid, 0, 103));
    CHECK(utxo_present(chain, matured_txid, 0, 103));

    // -------------------------------------------------------------------------
    // Sync continues. A header extending the new tip is a plain extension — if the
    // organizer were still anchored on the abandoned branch it would read this as
    // yet another fork and ask for a switch that can never be executed.
    // -------------------------------------------------------------------------
    auto const b104 = test::mine_block(branch_b.back().hash(), 104,
        base_time + 104 * block_spacing + 60, 2, {}, 0);
    auto const extension = fixture.organizer().add_headers(headers_of({b104}));

    CHECK(extension.headers_added == 1);
    CHECK_FALSE(extension.reorg_candidate);
    CHECK(fixture.organizer().header_height() == 104);
}
