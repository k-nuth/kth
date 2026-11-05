// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdio>
#include <filesystem>
#include <future>
#include <limits>
#include <catch2/generators/catch_generators.hpp>
#include <asio/use_future.hpp>
#include <kth/blockchain/abla_pipeline.hpp>

#include <chrono>
#include <future>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/reorg.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::node::sync;
using namespace kth::test;

TEST_CASE("returning to a previously connected branch survives a second restart",
          "[reorg][cycle][replayed_undo]") {
    test::chain_fixture fixture("replayed_undo_restart");
    REQUIRE(fixture.created());
    fixture.settings().max_reorg_depth = -1;
    REQUIRE(fixture.start());
    constexpr uint32_t fork_height = 12;
    auto const base = uint32_t(zulu_time()) - 60 * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= fork_height; ++h) {
        trunk.push_back(test::mine_block(previous, h, base + h * block_spacing, 0, {}, 0));
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    auto const make_branch = [&](uint32_t count, uint32_t salt) {
        std::vector<domain::chain::block> blocks;
        auto parent = trunk.back().hash();
        for (uint32_t n = 1; n <= count; ++n) {
            auto const h = fork_height + n;
            blocks.push_back(test::mine_block(parent, h, base + h * block_spacing, salt, {}, 0));
            parent = blocks.back().hash();
        }
        return blocks;
    };
    auto const a = make_branch(7, 1);
    std::vector<domain::chain::block> const a_first{a.front()};
    auto const b = make_branch(3, 2);
    REQUIRE(fixture.organizer().add_headers(headers_of(a_first)).headers_added == 1);
    persist_headers(fixture, a_first, fork_height + 1);
    connect_bodies(fixture, a_first, fork_height + 1);

    auto const adopt = [&](std::vector<domain::chain::block> const& branch) {
        auto const added = fixture.organizer().add_headers(headers_of(branch));
        REQUIRE(added.reorg_candidate);
        auto& chain = fixture.chain();
        ::asio::io_context io;
        auto switched = ::asio::co_spawn(io,
            execute_reorg(chain, fixture.organizer(), chain.headers().find(branch.back().hash()),
                fork_height, [] { return false; }, real_persister(chain)), ::asio::use_future);
        io.run();
        auto const outcome = switched.get();
        REQUIRE_FALSE(outcome.fatal);
        REQUIRE(outcome.result.ok);
        connect_bodies(fixture, branch, fork_height + 1);
        REQUIRE(chain.chain_view()->tip_hash == branch.back().hash());
    };
    adopt(b);
    REQUIRE(fixture.restart());
    // Startup restored only B's index. The bytes of A's first undo record
    // remain on disk, but replaying A can append the same record again.
    adopt(a);
    REQUIRE(utxo_present(fixture.chain(), a.front().transactions().front().hash(), 0, 19));
    REQUIRE_FALSE(utxo_present(fixture.chain(), b.front().transactions().front().hash(), 0, 19));
    REQUIRE(fixture.restart());
    auto const restored = fixture.chain().chain_view();
    REQUIRE(restored);
    CHECK(restored->tip_hash == a.back().hash());
    auto const heights = fixture.chain().get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 19);
    CHECK(utxo_present(fixture.chain(), a.front().transactions().front().hash(), 0, 19));
    CHECK_FALSE(utxo_present(fixture.chain(), b.front().transactions().front().hash(), 0, 19));
    auto const count = fixture.chain().utxo_count();
    REQUIRE(count);
    CHECK(*count == 19);
}

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
        auto const count = chain.utxo_count();
        REQUIRE(count);
        CHECK(*count == trunk_len);
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
        auto const count = chain.utxo_count();
        REQUIRE(count);
        CHECK(*count == trunk_len + 1u);
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

    // The published state as it stands on A, to compare against after the switch.
    auto const view_on_a = chain.chain_view();
    REQUIRE(view_on_a);
    REQUIRE(view_on_a->state->height() == 102u);
    REQUIRE(view_on_a->tip_hash == a101.hash());

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
                [] { return false; }, real_persister(chain)),
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

    // The organizer heard about the rewind. Deep-reorg parking measures depth
    // against this height, and the next header batch can arrive as soon as the
    // switch releases the pause — so a height left over from the branch just
    // abandoned would be read before anything corrected it.
    CHECK(fixture.organizer().validated_height() == int32_t(trunk_len));

    // The published state followed the switch, and followed it *down*: the
    // connected chain is back at the fork, so the height fell — while the
    // generation rose, because a state was published. That pairing is the whole
    // point of counting published states rather than heights; a counter derived
    // from the height would have gone backwards here, and a reader comparing it
    // would conclude nothing had changed (#605).
    //
    // The fork, not B's head: the switch rewound the connected chain, and B's
    // blocks are headers until the ordinary connect path applies their deltas.
    auto const view_on_b = chain.chain_view();
    REQUIRE(view_on_b);
    CHECK(view_on_b->state->height() == trunk_len + 1u);
    CHECK(view_on_b->state->height() < view_on_a->state->height());
    CHECK(view_on_b->tip_hash == trunk.back().hash());
    CHECK(view_on_b->tip_hash != view_on_a->tip_hash);
    CHECK(view_on_b->generation > view_on_a->generation);

    // A's block is disconnected: its coinbase is out of the set and the coinbase it
    // spent is back, at the height it was originally created — not at 101, which
    // would misdate its maturity and its median-time-past.
    CHECK_FALSE(utxo_present(chain, a101.transactions().front().hash(), 0, trunk_len));
    CHECK_FALSE(utxo_present(chain, spend_txid, 0, trunk_len));
    CHECK(utxo_present(chain, matured_txid, 0, trunk_len));
    {
        auto const count = chain.utxo_count();
        REQUIRE(count);
        CHECK(*count == trunk_len);
    }

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
        auto const count = chain.utxo_count();
        REQUIRE(count);
        CHECK(*count == trunk_len + branch_b.size());
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

    // Every expected output is present. Together with the exact count above,
    // this excludes unrelated entries left behind by the abandoned branch.
    for (auto const& blk : trunk) {
        CHECK(utxo_present(chain, blk.transactions().front().hash(), 0, 103));
    }

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

// =============================================================================
// The chain the node comes back on
// =============================================================================

TEST_CASE("a restart after a reorganization comes back on the branch that won", "[reorg][cycle]") {
    // The header index is rebuilt at startup from the persisted by-height headers,
    // so those are what decides which chain the node resumes. A switch that moved
    // the chain in memory without rewriting them would look fine for as long as
    // the process lived, and come back up on the branch it had abandoned — with
    // the UTXO set already rewound below it.
    //
    // Short chain on purpose: this is about what survives a restart, not about
    // spending a matured coinbase, which the cycle above covers.
    test::chain_fixture fixture("restart");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    constexpr uint32_t trunk_len = 12;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    // Branch A: one block on the trunk.
    auto const a13 = test::mine_block(trunk.back().hash(), 13,
        base_time + 13 * block_spacing, 1, {}, 0);
    REQUIRE(fixture.organizer().add_headers(headers_of({a13})).headers_added == 1);
    persist_headers(fixture, {a13}, 13);
    connect_bodies(fixture, {a13}, 13);

    // Branch B: three blocks from 12, outweighing it.
    std::vector<domain::chain::block> branch_b;
    prev = trunk.back().hash();
    for (uint32_t h = 13; h <= 15; ++h) {
        branch_b.push_back(test::mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = branch_b.back().hash();
    }
    auto const b_result = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(b_result.reorg_candidate);

    reorg_outcome reorg;
    {
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(fixture.chain(), fixture.organizer(), b_result.reorg_branch_head,
                trunk_len, [] { return false; }, real_persister(fixture.chain())),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
    }
    REQUIRE(reorg.result.ok);
    REQUIRE_FALSE(reorg.fatal);

    connect_bodies(fixture, branch_b, 13);

    // -------------------------------------------------------------------------
    // Everything above is in memory as much as on disk. Drop it all.
    // -------------------------------------------------------------------------
    REQUIRE(fixture.restart());

    auto& chain = fixture.chain();
    auto& index = chain.headers();

    // Rebuilt from disk: heights 13..15 name B's blocks, and the tip is B's head.
    CHECK(index.active_tip_height() == 15);
    for (uint32_t h = 13; h <= 15; ++h) {
        auto const idx = index.active_at(int32_t(h));
        REQUIRE(idx != blockchain::header_index::null_index);
        CHECK(index.get_hash(idx) == branch_b[h - 13].hash());
    }
    CHECK(fixture.organizer().tip_index() == index.active_at(15));

    // The abandoned branch is not in the index at all, which is worth stating
    // rather than discovering: the index is rebuilt from the by-height table, and
    // that table describes the active chain only. Side branches live in memory,
    // so a restart forgets them. Their block bytes stay in the flat files, held
    // by nothing, until a peer announces the branch again and the headers are
    // re-downloaded. (BCHN persists its whole block index and does remember.)
    CHECK(index.find(a13.hash()) == blockchain::header_index::null_index);

    // The validated tip and the UTXO set came back together, at B's head.
    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        CHECK(heights->block == 15);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        CHECK(*built == 15);
    }

    // And the organizer knows it. Nothing tells it until the first newly stored
    // block, so a node that came up and was handed a deep, heavier branch before
    // storing anything would measure the rewind against "nothing validated yet"
    // and follow it — with finalization not protecting either, since it needs
    // headers older than the finalization delay.
    CHECK(fixture.organizer().validated_height() == 15);

    // And the UTXO set is B's: its coinbases are there, A's is not.
    for (auto const& blk : branch_b) {
        CHECK(utxo_present(chain, blk.transactions().front().hash(), 0, 15));
    }
    CHECK_FALSE(utxo_present(chain, a13.transactions().front().hash(), 0, 15));

    // Nothing that is on the chain is A's: every active height names a B block or
    // a trunk one, so the switch did not leave the abandoned branch reachable by
    // height either.
    for (int32_t h = 13; h <= index.active_tip_height(); ++h) {
        auto const idx = index.active_at(h);
        REQUIRE(idx != blockchain::header_index::null_index);
        CHECK(index.get_hash(idx) != a13.hash());
    }
}

// =============================================================================
// When the persisted description cannot be updated
// =============================================================================

TEST_CASE("a switch whose header write fails is fatal, and the restart is coherent", "[reorg][cycle]") {
    // The write that makes a switch survivable is the one that re-describes the
    // replaced heights. If it fails, the chain in memory and the chain on disk
    // name different branches, and nothing repairs that while the node runs — so
    // execute_reorg reports it as fatal and the owner brings the node down.
    //
    // Reaching that path needs the write to fail on demand, which is why the
    // persister is a parameter rather than a call to the chain. Corrupting a real
    // database to get here would test the corruption, not the handling.
    test::chain_fixture fixture("fatal");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    constexpr uint32_t trunk_len = 12;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    auto const a13 = test::mine_block(trunk.back().hash(), 13,
        base_time + 13 * block_spacing, 1, {}, 0);
    REQUIRE(fixture.organizer().add_headers(headers_of({a13})).headers_added == 1);
    persist_headers(fixture, {a13}, 13);
    connect_bodies(fixture, {a13}, 13);

    std::vector<domain::chain::block> branch_b;
    prev = trunk.back().hash();
    for (uint32_t h = 13; h <= 15; ++h) {
        branch_b.push_back(test::mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = branch_b.back().hash();
    }
    auto const b_result = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(b_result.reorg_candidate);

    // -------------------------------------------------------------------------
    // The switch, with a write that refuses.
    // -------------------------------------------------------------------------
    bool persist_called = false;
    reorg_outcome reorg;
    {
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(fixture.chain(), fixture.organizer(), b_result.reorg_branch_head,
                trunk_len, [] { return false; },
                [&persist_called](domain::chain::header::list const&, size_t) {
                    persist_called = true;
                    return code(error::operation_failed);
                }),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
    }

    CHECK(persist_called);

    // The switch itself succeeded — the UTXO set was rewound and the chain moved.
    // What failed is describing that on disk, and it is that which is fatal.
    CHECK(reorg.result.ok);
    CHECK(reorg.fatal);

    // Height 13 still answers with A's block, the branch the node was on before:
    // a refused write leaves the table describing the old chain, and execute_reorg
    // does not change it by some other route on the way out.
    //
    // Not what this shows: that the write itself is atomic. The persister here
    // never reaches the database, so there is no transaction to have been left
    // half-applied. Atomicity is replace_headers_from's, and it is not exercised
    // from this test.
    {
        auto const persisted = fixture.chain().get_header(13);
        REQUIRE(persisted);
        CHECK(domain::chain::hash(*persisted) == a13.hash());
    }

    // No blocks are connected for B. In the node this is the coordinator refusing
    // to send more work; here it is simply not sending any.

    // -------------------------------------------------------------------------
    // The restart the owner's shutdown leads to.
    // -------------------------------------------------------------------------
    REQUIRE(fixture.restart());

    auto& chain = fixture.chain();
    auto& index = chain.headers();

    // Back on A, whole: the persisted table never described anything else.
    CHECK(index.active_tip_height() == 13);
    {
        auto const idx = index.active_at(13);
        REQUIRE(idx != blockchain::header_index::null_index);
        CHECK(index.get_hash(idx) == a13.hash());
    }

    // The UTXO set and the validated height are where the switch left them — at
    // the fork — so the node re-downloads A's block at 13 rather than trusting a
    // UTXO state that no longer matches the chain it came back on.
    {
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        CHECK(heights->block == trunk_len);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        CHECK(*built == trunk_len);
    }

    // A's coinbase at 13 is not in the set: its block was disconnected before the
    // write failed, and the markers agree with that.
    CHECK_FALSE(utxo_present(chain, a13.transactions().front().hash(), 0, trunk_len));

    // The organizer came back knowing how far blocks are validated, so the retry
    // below is judged with deep-reorg parking active rather than switched off.
    CHECK(fixture.organizer().validated_height() == int32_t(trunk_len));

    // And the heavier branch can be tried again. Its headers were forgotten with
    // the restart (side branches are not persisted), so a peer re-announcing them
    // is what a real node would see; the switch is then reachable exactly as
    // before. It forks at the validated tip itself, so it rewinds nothing and
    // parking does not hold it — which is the rule applying, not the rule being
    // absent.
    auto const retry = fixture.organizer().add_headers(headers_of(branch_b));
    CHECK(retry.reorg_candidate);
    CHECK(retry.reorg_fork_height == int32_t(trunk_len));
}

TEST_CASE("a reorganization after a restart can disconnect what was connected before it",
          "[reorg][cycle][restart]") {
    // The restart test above reorganizes and *then* restarts, so nothing it
    // disconnects was connected in an earlier process. This is the other order,
    // and it is the one that needs undo data to survive: the blocks being rolled
    // back were connected before the node came down.
    //
    // Undo records live in the rev files and their positions live in the header
    // index, which is rebuilt at startup. Whether that rebuild restores them is
    // exactly what this pins.
    test::chain_fixture fixture("reorg_after_restart");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    constexpr uint32_t trunk_len = 12;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    // Branch A, connected in this process: two blocks that a later branch will
    // have to roll back.
    std::vector<domain::chain::block> branch_a;
    prev = trunk.back().hash();
    for (uint32_t h = 13; h <= 14; ++h) {
        branch_a.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 1, {}, 0));
        prev = branch_a.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(branch_a)).headers_added == 2);
    persist_headers(fixture, branch_a, 13);
    connect_bodies(fixture, branch_a, 13);

    {
        auto const built = fixture.chain().get_utxo_built_height();
        REQUIRE(built);
        REQUIRE(*built == 14);
    }

    // Undo data exists for them, in this process.
    {
        auto& index = fixture.chain().headers();
        for (uint32_t h = 13; h <= 14; ++h) {
            auto const idx = index.active_at(int32_t(h));
            REQUIRE(idx != blockchain::header_index::null_index);
            INFO("before the restart, height " << h);
            CHECK(index.has_undo_data(idx));
        }
    }

    // -------------------------------------------------------------------------
    // Drop everything held in memory. The rev files stay on disk.
    // -------------------------------------------------------------------------
    REQUIRE(fixture.restart());

    {
        auto& index = fixture.chain().headers();
        for (uint32_t h = 13; h <= 14; ++h) {
            auto const idx = index.active_at(int32_t(h));
            REQUIRE(idx != blockchain::header_index::null_index);
            INFO("after the restart, height " << h);
            CHECK(index.has_undo_data(idx));
        }
    }

    // Branch B forks at 12 and outweighs A, so switching to it has to disconnect
    // 13 and 14 — the two connected before the restart.
    std::vector<domain::chain::block> branch_b;
    prev = trunk.back().hash();
    for (uint32_t h = 13; h <= 15; ++h) {
        branch_b.push_back(test::mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = branch_b.back().hash();
    }
    auto const b_result = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(b_result.reorg_candidate);

    reorg_outcome reorg;
    {
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(fixture.chain(), fixture.organizer(), b_result.reorg_branch_head,
                trunk_len, [] { return false; }, real_persister(fixture.chain())),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
    }

    INFO("the switch has to disconnect two blocks connected before the restart");
    REQUIRE(reorg.result.ok);
    REQUIRE_FALSE(reorg.fatal);

    connect_bodies(fixture, branch_b, 13);

    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 15);
}

TEST_CASE("an undo record whose block hash was altered stops the node starting",
          "[reorg][cycle][restart]") {
    // What justifies skipping a record whose block the index does not hold. That
    // is ordinary for a branch that lost a reorganization, and locally
    // indistinguishable from an active record with a flipped bit in its stored
    // hash — which would otherwise leave a connected block quietly without undo,
    // unable to be disconnected, and nothing would say so.
    //
    // The separation is global rather than local: every block on the active chain
    // that was connected must still have its undo. Altering one hash makes that
    // coverage short, and the start has to refuse.
    test::chain_fixture fixture("undo_hash_altered");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    constexpr uint32_t trunk_len = 6;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    {
        auto const built = fixture.chain().get_utxo_built_height();
        REQUIRE(built);
        REQUIRE(*built == trunk_len);
    }

    // Flip a bit of the first record's stored block hash, which sits four bytes
    // past the marker. The checksum still passes — it covers the payload and is
    // seeded with the parent — so nothing but the coverage check can catch this.
    auto const rev_path = fixture.dir() / "blocks" / "rev00000.dat";
    REQUIRE(std::filesystem::exists(rev_path));
    {
        FILE* file = std::fopen(rev_path.string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, 4, SEEK_SET) == 0);
        int const original = std::fgetc(file);
        REQUIRE(original != EOF);
        REQUIRE(std::fseek(file, 4, SEEK_SET) == 0);
        REQUIRE(std::fputc(original ^ 0x01, file) != EOF);
        std::fclose(file);
    }

    // The altered record now names a block the index does not hold, so the scan
    // passes it over — and the block it belonged to is left without undo.
    CHECK_FALSE(fixture.restart());
}


#if defined(KTH_CURRENCY_BCH)
TEST_CASE("short-block chains survive a deep reorganization in both directions and restart",
          "[reorg][cycle][faster_blocks][deep]") {
    auto const depth = GENERATE(200u, 300u, 1088u);
    CAPTURE(depth);
    constexpr uint32_t activation_height = 120;
    constexpr uint32_t fork_height = 1010;
    constexpr uint64_t fee = 1000;
    auto const schedule = domain::chain::block_ticks::activated_at(activation_height);
    auto const b_length = 2 * depth + 1;
    auto const final_a_length = 2 * b_length + 1;
    auto const base = uint32_t(zulu_time())
        - (fork_height + final_a_length + 40) * 60 - activation_height * 540;
    auto const timestamp = [&](uint32_t h) {
        return base + uint32_t(schedule.height_to_tick(h));
    };
    // Keep the whole generated history in the past. The generous recency
    // limit lets the ordinary connect path drain partial batches while this
    // test exercises consensus/reorg durability rather than IBD freshness.
    test::chain_fixture fixture("short_deep_cycle", 14u * 24u);
    REQUIRE(fixture.created());
    fixture.settings().cantor_activation_time = timestamp(activation_height - 5);
    fixture.settings().max_reorg_depth = -1; // Exercise consensus, not automatic finalization.
    REQUIRE(fixture.start());

    // All headers use the modern 0x20000000 version (>= 4), including regtest heights
    // 1251/1351. Those upgrades remain height-based when maturity uses ticks.
    auto const mine = [&](hash_digest const& prev, uint32_t h, uint32_t salt,
                          std::vector<domain::chain::transaction> txs, uint64_t fees) {
        domain::chain::transaction::list all;
        auto cb = test::coinbase_tx(h, salt,
            domain::chain::block::subsidy(schedule, h, false) + fees);
        // The activation parent still receives the legacy subsidy, but Cantor's
        // coinbase-shape rule already applies there. Use version 2 throughout.
        cb = domain::chain::transaction(2, 0, domain::chain::input::list(cb.inputs()),
            domain::chain::output::list(cb.outputs()));
        if (salt == 2) {
            auto inputs = cb.inputs();
            auto ops = inputs.front().script().operations();
            ops.emplace_back(data_chunk(20, 0x42), false);
            inputs.front().set_script(domain::chain::script(std::move(ops)));
            cb = domain::chain::transaction(2, 0, std::move(inputs),
                domain::chain::output::list(cb.outputs()));
        }
        all.push_back(std::move(cb));
        for (auto& tx : txs) all.push_back(std::move(tx));
        domain::chain::block candidate(domain::chain::header{
            0x20000000, prev, null_hash, timestamp(h), test::regtest_bits, 0}, std::move(all));
        auto const merkle = candidate.generate_merkle_root();
        for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
            domain::chain::header header{0x20000000, prev, merkle, timestamp(h),
                test::regtest_bits, uint32_t(nonce)};
            if (header.is_valid_proof_of_work(domain::chain::hash(header), false))
                return domain::chain::block(std::move(header),
                    domain::chain::transaction::list(candidate.transactions()));
        }
        throw std::runtime_error("regtest nonce space exhausted");
    };

    // Join both production connect tasks and propagate their exceptions. Fatal
    // observations are checked on the test thread, never from an Asio callback.
    auto const connect = [&](std::vector<domain::chain::block> const& blocks, uint32_t first) {
        auto& chain = fixture.chain();
        auto const last = first + uint32_t(blocks.size()) - 1;
        bool stop = false;
        ::asio::io_context io;
        block_storage_input_channel input(io.get_executor(), 2);
        chunk_validated_channel output(io.get_executor(), 2);
        std::atomic<uint32_t> contiguous{first};
        std::vector<std::shared_ptr<domain::chain::light_block const>> light;
        for (auto const& blk : blocks) light.push_back(to_light(blk));
        std::string fatal;
        auto storage = ::asio::co_spawn(io,
            block_storage_task(chain, input, output, first, fixture.organizer(), &contiguous),
            ::asio::use_future);
        auto builder = ::asio::co_spawn(io,
            utxo_build_task(chain, contiguous, first, domain::config::network::regtest,
                [&] { auto h = chain.get_utxo_built_height(); return stop || (h && *h >= last); },
                [&](std::string const& why) { fatal = why; }), ::asio::use_future);
        REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
            .start_height = first, .chunk_id = 0, .blocks = std::move(light),
            .source_peer = nullptr, .generation = chain.headers().generation()}));
        REQUIRE(input.try_send(std::error_code{}, stop_request{}));
        io.run_for(std::chrono::seconds(120));
        auto const completed = storage.wait_for(std::chrono::seconds(0)) == std::future_status::ready
            && builder.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (!completed) {
            // Wake the owned tasks before reporting the deadline on this thread.
            // io's destruction also destroys any remaining suspended coroutine;
            // no detached task, worker thread or borrowed fixture escapes.
            stop = true;
            input.close();
            output.close();
            io.restart();
            io.run_for(std::chrono::seconds(5));
        }
        REQUIRE(completed);
        storage.get();
        builder.get();
        REQUIRE(fatal.empty());
        REQUIRE(chain.get_utxo_built_height());
        REQUIRE(*chain.get_utxo_built_height() == last);
    };
    auto const switch_branch = [&](hash_digest const& tip) {
        auto& chain = fixture.chain();
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io,
            execute_reorg(chain, fixture.organizer(), chain.headers().find(tip), fork_height,
                [] { return false; }, real_persister(chain)), ::asio::use_future);
        io.run_for(std::chrono::seconds(120));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        auto const outcome = result.get();
        REQUIRE_FALSE(outcome.fatal);
        REQUIRE(outcome.result.ok);
        REQUIRE(outcome.result.validated_tip == fork_height);
    };

    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= fork_height; ++h) {
        trunk.push_back(mine(prev, h, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    connect(trunk, 1);
    REQUIRE(fixture.chain().chain_view());
    REQUIRE(fixture.chain().chain_view()->state->ticks() == schedule);
    REQUIRE(fixture.chain().chain_view()->state->block_interval() == 60);
    auto const funding = trunk.front().transactions().front();
    REQUIRE(schedule.height_to_tick(fork_height + 1) - schedule.height_to_tick(1) >= 60000);

    auto const branch = [&](uint32_t length, uint32_t salt) {
        std::vector<domain::chain::block> blocks;
        auto parent = trunk.back().hash();
        auto source = funding;
        for (uint32_t n = 0; n < length; ++n) {
            auto spend = test::spend_p2pkh(source, 0, source.outputs()[0].value() - fee - salt,
                fixture.chain().chain_settings().enabled_flags());
            blocks.push_back(mine(parent, fork_height + n + 1, salt, {spend}, fee + salt));
            parent = blocks.back().hash();
            source = std::move(spend);
        }
        return blocks;
    };
    auto a = branch(depth, 1);
    // At these depths parking requires strictly more than twice the defending
    // branch's post-fork work. Exercise that real policy, without overriding it.
    auto const b = branch(b_length, 2);
    REQUIRE(fixture.organizer().add_headers(headers_of(a)).headers_added == a.size());
    persist_headers(fixture, a, fork_height + 1);
    connect(a, fork_height + 1);

    auto const check_tip = [&](std::vector<domain::chain::block> const& active,
                               std::vector<domain::chain::block> const& abandoned) {
        auto& chain = fixture.chain();
        auto const height = fork_height + uint32_t(active.size());
        REQUIRE(chain.get_last_heights());
        CHECK(chain.get_last_heights()->block == height);
        REQUIRE(chain.chain_view());
        CHECK(chain.chain_view()->tip_hash == active.back().hash());
        CHECK(chain.chain_view()->state->block_interval() == 60);
        CHECK(chain.chain_view()->state->ticks().height_to_tick(height) == schedule.height_to_tick(height));
        CHECK_FALSE(utxo_present(chain, funding.hash(), 0, height));
        CHECK(utxo_present(chain, active.back().transactions()[1].hash(), 0, height));
        CHECK_FALSE(utxo_present(chain, abandoned.back().transactions()[1].hash(), 0, height));
        auto const fork_row = chain.get_header_and_abla_state(fork_height);
        REQUIRE(fork_row);
        domain::chain::abla::state expected;
        expected.block_size = std::get<1>(*fork_row);
        expected.control_block_size = std::get<2>(*fork_row);
        expected.elastic_buffer_size = std::get<3>(*fork_row);
        expected.block_interval = 60;
        for (auto const& blk : active) {
            auto const next_state = domain::chain::abla::next(expected,
                chain.chain_settings().abla_config, blk.serialized_size(), 60);
            REQUIRE(next_state);
            expected = *next_state;
            CHECK(utxo_present(chain, blk.transactions().front().hash(), 0, height));
            auto const h = fork_height + uint32_t(&blk - active.data()) + 1;
            auto const row = chain.get_header_and_abla_state(h);
            REQUIRE(row);
            CHECK(domain::chain::hash(std::get<0>(*row)) == blk.hash());
            CHECK(std::get<1>(*row) == blk.serialized_size());
            CHECK(std::get<2>(*row) == expected.control_block_size);
            CHECK(std::get<3>(*row) == expected.elastic_buffer_size);
        }
        for (auto const& blk : abandoned)
            CHECK_FALSE(utxo_present(chain, blk.transactions().front().hash(), 0, height));
        REQUIRE(chain.utxo_count());
        CHECK(*chain.utxo_count() == fork_height + active.size());
        auto const row = chain.get_header_and_abla_state(height);
        REQUIRE(row);
        domain::chain::abla::state tip_state;
        tip_state.block_size = std::get<1>(*row);
        tip_state.control_block_size = std::get<2>(*row);
        tip_state.elastic_buffer_size = std::get<3>(*row);
        tip_state.block_interval = 60;
        auto const next = domain::chain::abla::next(tip_state, chain.chain_settings().abla_config, 0, 60);
        REQUIRE(next);
        CHECK(chain.chain_view()->state->abla_state().control_block_size == next->control_block_size);
        CHECK(chain.chain_view()->state->abla_state().elastic_buffer_size == next->elastic_buffer_size);
    };

    auto const b_headers = fixture.organizer().add_headers(headers_of(b));
    REQUIRE(b_headers.reorg_candidate);
    switch_branch(b.back().hash());
    connect(b, fork_height + 1);
    check_tip(b, a);
    REQUIRE(fixture.restart());
    check_tip(b, a);

    // A's old bytes and undo history remain available. Extend A beyond B and
    // replay its whole branch, including the chained spends previously undone.
    auto parent = a.back().hash();
    auto source = a.back().transactions()[1];
    std::vector<domain::chain::block> extension;
    for (uint32_t n = 1; n <= final_a_length - depth; ++n) {
        auto spend = test::spend_p2pkh(source, 0, source.outputs()[0].value() - fee - 1,
            fixture.chain().chain_settings().enabled_flags());
        extension.push_back(mine(parent, fork_height + depth + n, 1, {spend}, fee + 1));
        parent = extension.back().hash();
        source = std::move(spend);
    }
    // Restart reconstructs only the active branch; resubmit A's headers too.
    std::vector<domain::chain::block> full_a = a;
    full_a.insert(full_a.end(), extension.begin(), extension.end());
    auto const a_headers = fixture.organizer().add_headers(headers_of(full_a));
    REQUIRE(a_headers.reorg_candidate);
    switch_branch(full_a.back().hash());
    connect(full_a, fork_height + 1);
    check_tip(full_a, b);
    REQUIRE(fixture.restart());
    check_tip(full_a, b);
}
#endif
