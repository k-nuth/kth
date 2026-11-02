// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/node/sync/orchestrator.hpp>
#include "sync_harness.hpp"
#include <asio/use_future.hpp>
#include <asio/steady_timer.hpp>
#include <future>
using namespace kth;
using namespace kth::test;

TEST_CASE("stored invalid bodies cannot become the validated or finalized tip", "[stored_tip_finalization]") {
    chain_fixture fixture("stored_invalid_finality");
    REQUIRE(fixture.created());
    fixture.settings().finalization_delay_seconds = 0;
    fixture.settings().max_reorg_depth = 10;
    fixture.settings().max_checkpoint_height = 0;
    fixture.settings().cantor_activation_time = UINT64_MAX;
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const initial_built = chain.get_utxo_built_height();
    REQUIRE(initial_built.value_or(0) == 0);
    constexpr uint32_t count = 20;
    auto const base_time = uint32_t(zulu_time()) - (count + 30) * block_spacing;
    std::vector<domain::chain::block> blocks;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= count; ++height) {
        // Merkle/PoW and syntax valid, but one satoshi of nonexistent fees
        // overpays every coinbase. Full validation must reject the first block.
        blocks.push_back(mine_block(previous, height, base_time + height * block_spacing, 0, {}, 1));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == count);
    persist_headers(fixture, blocks, 1);
    std::vector<std::shared_ptr<domain::message::block const>> full;
    for (auto const& block : blocks) full.push_back(std::make_shared<domain::message::block const>(block));
    REQUIRE(blockchain::validate_block_batch(chain, chain.chain_settings(), domain::config::network::regtest,
        full, 1) == error::coinbase_value_limit);

    // Same real tasks as run_connect_tasks, but retain completion futures and
    // run until both tasks return rather than ending on the harness's deadline.
    // All setup assertions happen before either coroutine is launched.
    ::asio::io_context ctx;
    node::sync::block_storage_input_channel input(ctx.get_executor(), 16);
    node::sync::chunk_validated_channel output(ctx.get_executor(), 256);
    std::atomic<uint32_t> contiguous{1}; // Existing production task interface.
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& block : blocks) light.push_back(to_light(block));
    REQUIRE(input.try_send(std::error_code{}, node::sync::downloaded_chunk{
        .start_height = 1, .chunk_id = 0, .blocks = std::move(light),
        .source_peer = nullptr, .generation = chain.headers().generation()}));
    REQUIRE(input.try_send(std::error_code{}, node::sync::stop_request{}));
    std::vector<std::string> fatals;
    auto storage = ::asio::co_spawn(ctx,
        node::sync::block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::use_future);
    auto writer = ::asio::co_spawn(ctx,
        node::sync::utxo_build_task(chain, contiguous, 1, domain::config::network::regtest,
            [&chain] { auto const built = chain.get_utxo_built_height(); return built && *built >= count; },
            [&fatals](std::string const& reason) { fatals.push_back(reason); }),
        ::asio::use_future);
    ctx.run();
    storage.get();
    writer.get();
    CHECK(fatals.empty());
    CHECK(output.try_receive([](std::error_code ec, node::sync::chunk_validated result) {
        CHECK_FALSE(ec);
        CHECK(result.result == error::success);
        CHECK(result.block_count == count);
    }));
    auto const built = chain.get_utxo_built_height();
    CHECK(built == initial_built);
    CHECK(built.value_or(0) == 0);
    REQUIRE(chain.chain_state());
    CHECK(chain.chain_state()->height() == 1);
    CHECK(fixture.organizer().validated_height() <= int32_t(built.value_or(0)));
    CHECK(fixture.organizer().finalized_height() <= int32_t(built.value_or(0)));
}

TEST_CASE("connected publication delivers finality progress without a polling bridge",
          "[stored_tip_finalization]") {
    chain_fixture fixture("connected_finality_progress");
    REQUIRE(fixture.created());
    fixture.settings().finalization_delay_seconds = 0;
    fixture.settings().max_reorg_depth = 10;
    fixture.settings().max_checkpoint_height = 0;
    fixture.settings().cantor_activation_time = UINT64_MAX;
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    constexpr uint32_t count = 20;
    auto const base_time = uint32_t(zulu_time()) - (count + 30) * block_spacing;
    std::vector<domain::chain::block> blocks;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= count; ++height) {
        blocks.push_back(mine_block(previous, height, base_time + height * block_spacing, 0, {}, 0));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == count);
    persist_headers(fixture, blocks, 1);
    ::asio::io_context ctx;
    node::sync::block_storage_input_channel input(ctx.get_executor(), 16);
    node::sync::chunk_validated_channel output(ctx.get_executor(), 16);
    node::sync::utxo_build_progress_channel progress(ctx.get_executor(), 1);
    std::atomic<uint32_t> contiguous{1};
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& block : blocks) light.push_back(to_light(block));
    REQUIRE(input.try_send(std::error_code{}, node::sync::downloaded_chunk{
        .start_height = 1, .chunk_id = 0, .blocks = std::move(light),
        .source_peer = nullptr, .generation = chain.headers().generation()}));
    REQUIRE(input.try_send(std::error_code{}, node::sync::stop_request{}));
    bool closed = false;
    bool full = false;
    SECTION("bounded progress is delivered after the connected view") {}
    SECTION("a full progress channel coalesces publication without blocking the builder") {
        full = true;
        REQUIRE(progress.try_send(std::error_code{}, node::sync::utxo_build_advanced{9999}));
    }
    SECTION("closure before the send wakes the builder after publication") {
        closed = true;
        progress.close();
    }
    // A broken implementation that waits on the full progress channel must
    // still release every owned coroutine before the assertion reports it.
    bool deadline_fired = false;
    ::asio::steady_timer watchdog(ctx);
    if (full) {
        watchdog.expires_after(std::chrono::seconds(30));
        watchdog.async_wait([&](std::error_code ec) {
            if (ec) return;
            deadline_fired = true;
            progress.cancel();
            progress.close();
        });
    }
    std::vector<std::string> fatals;
    auto storage = ::asio::co_spawn(ctx,
        node::sync::block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::use_future);
    auto writer = ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        try {
            co_await node::sync::utxo_build_task(chain, contiguous, 1,
                domain::config::network::regtest,
                [&chain] { auto const built = chain.get_utxo_built_height(); return built && *built >= count; },
                [&fatals](std::string const& reason) { fatals.push_back(reason); }, &progress);
        } catch (...) {
            watchdog.cancel();
            throw;
        }
        watchdog.cancel();
    }, ::asio::use_future);
    ctx.run();
    storage.get();
    writer.get();
    CHECK_FALSE(deadline_fired);
    CHECK(fatals.empty());
    REQUIRE(chain.chain_view());
    CHECK(chain.chain_view()->connected_tip_height == count);
    CHECK(chain.chain_view()->tip_hash == blocks.back().hash());
    // Storage and building alone never mutate the coordinator's finality state.
    CHECK(fixture.organizer().validated_height() <= 0);
    CHECK(fixture.organizer().finalized_height() <= 0);
    bool delivered = false;
    progress.try_receive([&](std::error_code ec, node::sync::utxo_build_advanced event) {
        CHECK_FALSE(ec);
        CHECK(event.built_height == (full ? 9999 : count));
        delivered = true;
    });
    CHECK(delivered == ! closed);
    REQUIRE(node::sync::refresh_validated_tip(chain, fixture.organizer()));
    CHECK(fixture.organizer().validated_height() == count);
    CHECK(fixture.organizer().finalized_height() == count - 10);

    // A queued notification may outlive a branch switch. Leave the old immutable
    // publication in place while the active height mapping moves, as it does
    // inside a reorganization, and verify it cannot restore the old tip height.
    auto const old_view = chain.chain_view();
    std::vector<domain::chain::block> fork;
    auto fork_previous = blocks[16].hash();
    for (uint32_t height = 18; height <= 21; ++height) {
        fork.push_back(mine_block(fork_previous, height,
            base_time + height * block_spacing + 1, 0, {}, 0));
        fork_previous = fork.back().hash();
    }
    auto const candidate = fixture.organizer().add_headers(headers_of(fork));
    REQUIRE(candidate.reorg_candidate);
    REQUIRE(candidate.reorg_fork_height == 17);
    auto const fork_tip = chain.headers().find(fork.back().hash());
    REQUIRE(fork_tip != blockchain::header_index::null_index);
    fixture.organizer().adopt_tip(fork_tip);
    fixture.organizer().note_block_validated(17);
    REQUIRE(chain.chain_view() == old_view);
    CHECK_FALSE(node::sync::refresh_validated_tip(chain, fixture.organizer()));
    CHECK(fixture.organizer().validated_height() == 17);
    CHECK(fixture.organizer().finalized_height() == count - 10);

}

TEST_CASE("connection harness joins stalled tasks at its deadline", "[connect_watchdog]") {
    chain_fixture fixture("connection_watchdog");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const initial = fixture.chain().get_utxo_built_height();
    auto const block = mine_block(domain::chain::block::genesis_regtest().hash(),
        1, uint32_t(zulu_time()) - block_spacing, 0, {}, 0);
    // No active header is admitted. Storage cannot publish contiguous progress,
    // so the real UTXO task cannot satisfy its normal completion predicate.
    std::vector<std::string> fatals;
    CHECK_FALSE(run_connect_tasks(fixture, {block}, 1,
        [&fatals](std::string const& reason) { fatals.push_back(reason); },
        std::chrono::milliseconds(50)));
    CHECK(fatals.empty());
    CHECK(fixture.chain().get_utxo_built_height() == initial);
    CHECK(fixture.chain().reorg_barrier_state() == std::make_pair(size_t{0}, size_t{0}));
}
