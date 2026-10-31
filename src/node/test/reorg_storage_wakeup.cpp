// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <asio/steady_timer.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/use_future.hpp>
#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

TEST_CASE("a reorganization wakes storage and resets its cursor before resuming",
          "[reorg][storage_wakeup]") {
    chain_fixture fixture("reorg_idle_storage");
    REQUIRE(fixture.created());
    fixture.settings().max_reorg_depth = -1;
    fixture.settings().max_checkpoint_height = 0;
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const base = uint32_t(zulu_time()) - 30 * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 6; ++h) {
        trunk.push_back(mine_block(previous, h, base + h * block_spacing, 0, {}, 0));
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, {trunk[0], trunk[1], trunk[2], trunk[3], trunk[4]}, 1);
    REQUIRE(chain.chain_view()->connected_tip_height == 5);
    auto const unfetched = chain.headers().find(trunk[5].hash());
    REQUIRE(unfetched != blockchain::header_index::null_index);
    REQUIRE_FALSE(chain.headers().has_status(unfetched, blockchain::header_status::have_data));

    std::vector<domain::chain::block> fork;
    previous = trunk[0].hash();
    for (uint32_t h = 2; h <= 10; ++h) {
        fork.push_back(mine_block(previous, h, base + h * block_spacing + 30, 2, {}, 0));
        previous = fork.back().hash();
    }
    auto const candidate = fixture.organizer().add_headers(headers_of(fork));
    REQUIRE(candidate.reorg_candidate);
    REQUIRE(candidate.reorg_fork_height == 1);
    REQUIRE(chain.reorg_barrier_state() == std::make_pair(size_t{0}, size_t{0}));

    auto const pending_block = to_light(trunk[5]);
    auto const replacement = to_light(fork.front());
    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 1);
    chunk_validated_channel output(ctx.get_executor(), 8);
    std::atomic<uint32_t> contiguous{6};
    auto storage = ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 6, fixture.organizer(), &contiguous),
        ::asio::use_future);
    // Run until the real task suspends on empty input, deterministically after
    // its initial pause check. No sleeping thread or scheduling race is needed.
    ctx.poll();
    auto const waiting = chain.reorg_barrier_state();
    bool queued = false;
    bool full_case = false;
    SECTION("the input remains empty") {}
    SECTION("pause follows receive completion with a full input queue") {
        full_case = true;
        auto const make_chunk = [&] {
            return downloaded_chunk{
                .start_height = 6, .chunk_id = 0, .blocks = {pending_block},
                .source_peer = nullptr, .generation = chain.chain_generation()};
        };
        // Complete the pending receive, then fill its capacity-one queue. Keep
        // the executor stopped until pause is raised between receive and store.
        queued = input.try_send(std::error_code{}, make_chunk());
        queued = input.try_send(std::error_code{}, make_chunk()) && queued;
        chain.request_reorg_pause(true);
    }
    bool deadline_fired = false;
    bool switch_finished = false;
    bool replacement_ack = false;
    uint32_t cursor_after_switch = 0;
    uint32_t cursor_after_ack = 0;
    std::vector<chunk_validated> acknowledgments;
    ::asio::steady_timer deadline(ctx);
    deadline.expires_after(std::chrono::seconds(5));
    deadline.async_wait([&](std::error_code ec) {
        if (ec) return;
        deadline_fired = true;
        // Before the switch returns only request abort: retiring storage here
        // would hide the missed wake by making its barrier trivially satisfied.
        if (switch_finished) {
            input.cancel(); input.close();
            output.cancel(); output.close();
        }
    });
    reorg_outcome outcome;
    auto coordinator = ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        try {
            outcome = co_await execute_reorg(chain, fixture.organizer(),
                candidate.reorg_branch_head, 1, [&] { return deadline_fired; },
                real_persister(chain), reorg_pipeline{&input, &contiguous});
            switch_finished = true;
            cursor_after_switch = contiguous.load();
            if (outcome.result.ok) {
                // Deliberately do not reset the cursor in the caller. Let
                // storage resume and acknowledge only the FIRST new body: an
                // old cursor of 6 would skip the real gap above height 2.
                downloaded_chunk next{
                    .start_height = 2, .chunk_id = 1, .blocks = {replacement},
                    .source_peer = nullptr, .generation = chain.chain_generation()};
                auto sent = co_await input.async_send(std::error_code{}, std::move(next),
                    ::asio::as_tuple(::asio::use_awaitable));
                if ( ! std::get<0>(sent)) {
                    while (true) {
                        auto received = co_await output.async_receive(::asio::as_tuple(::asio::use_awaitable));
                        if (std::get<0>(received)) break;
                        auto const& ack = std::get<1>(received);
                        acknowledgments.push_back(ack);
                        if (ack.start_height == 2) {
                            replacement_ack = true;
                            cursor_after_ack = contiguous.load();
                            break;
                        }
                    }
                }
            }
        } catch (...) {
            deadline.cancel();
            input.cancel(); input.close();
            output.cancel(); output.close();
            throw;
        }
        deadline.cancel();
        input.cancel(); input.close();
    }, ::asio::use_future);
    ctx.run();
    coordinator.get();
    storage.get();

    CHECK(waiting == std::make_pair(size_t{0}, size_t{1}));
    CHECK_FALSE(deadline_fired);
    CHECK_FALSE(outcome.fatal);
    CHECK(outcome.result.ok);
    CHECK(outcome.result.validated_tip == std::optional<uint32_t>{1});
    CHECK(fixture.organizer().header_tip_hash() == fork.back().hash());
    auto const persisted = chain.get_header(2);
    REQUIRE(persisted);
    CHECK(domain::chain::hash(*persisted) == fork.front().hash());
    CHECK(chain.chain_view()->connected_tip_height == 1);
    CHECK(chain.chain_view()->tip_hash == trunk.front().hash());
    CHECK_FALSE(chain.headers().has_status(unfetched, blockchain::header_status::have_data));
    CHECK(chain.reorg_barrier_state() == std::make_pair(size_t{0}, size_t{0}));
    CHECK_FALSE(chain.reorg_pause_requested());
    CHECK(queued == full_case);
    CHECK(cursor_after_switch == 2);
    CHECK(replacement_ack);
    CHECK(cursor_after_ack == 3);
    REQUIRE(acknowledgments.size() == 1);
    CHECK(acknowledgments.front().start_height == 2);
    CHECK(acknowledgments.front().block_count == 1);
    CHECK(acknowledgments.front().result == error::success);
}


TEST_CASE("storage cancellation retires its reorg registration and parked count",
          "[reorg][storage_wakeup][cancel]") {
    chain_fixture fixture("reorg_storage_cancel");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    bool parked_first = false;
    SECTION("cancel before the coroutine starts") {}
    SECTION("cancel after the real task parks") {
        parked_first = true;
        chain.request_reorg_pause(true);
    }
    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 1);
    chunk_validated_channel output(ctx.get_executor(), 1);
    ::asio::cancellation_signal cancellation;
    auto task = ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 1, fixture.organizer()),
        ::asio::bind_cancellation_slot(cancellation.slot(), ::asio::use_future));
    if (parked_first) ctx.poll();
    auto const before = chain.reorg_barrier_state();
    cancellation.emit(::asio::cancellation_type::terminal);
    ctx.restart();
    ctx.run();
    std::error_code result;
    try { task.get(); }
    catch (std::system_error const& error) { result = error.code(); }
    auto const after = chain.reorg_barrier_state();
    chain.request_reorg_pause(false);
    if (parked_first) CHECK(result == ::asio::error::operation_aborted);
    else CHECK(( ! result || result == ::asio::error::operation_aborted));
    if (parked_first) CHECK(before == std::make_pair(size_t{1}, size_t{1}));
    CHECK(after == std::make_pair(size_t{0}, size_t{0}));
}

TEST_CASE("an aborted pause preserves a received chunk on the unchanged branch",
          "[reorg][storage_wakeup][retained_chunk]") {
    chain_fixture fixture("reorg_retained_chunk");
    REQUIRE(fixture.created());
    fixture.settings().max_reorg_depth = -1;
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const base = uint32_t(zulu_time()) - 30 * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 3; ++h) {
        trunk.push_back(mine_block(previous, h, base + h * block_spacing, 0, {}, 0));
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, {trunk[0], trunk[1]}, 1);
    auto const held = to_light(trunk[2]);
    auto const idx = chain.headers().find(trunk[2].hash());
    REQUIRE(idx != blockchain::header_index::null_index);
    REQUIRE_FALSE(chain.headers().has_status(idx, blockchain::header_status::have_data));
    auto const generation = chain.chain_generation();
    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 1);
    chunk_validated_channel output(ctx.get_executor(), 2);
    std::atomic<uint32_t> contiguous{3};
    auto storage = ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, 3, fixture.organizer(), &contiguous),
        ::asio::use_future);
    ctx.poll();
    auto const accepted = input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = 3, .chunk_id = 0, .blocks = {held},
        .source_peer = nullptr, .generation = generation});
    chain.request_reorg_pause(true);
    ctx.poll();
    auto const parked = chain.reorg_barrier_state();
    auto const stored_before_resume = chain.headers().has_status(idx, blockchain::header_status::have_data);
    bool deadline_fired = false;
    bool ack_received = false;
    chunk_validated ack;
    ::asio::steady_timer deadline(ctx);
    deadline.expires_after(std::chrono::seconds(5));
    deadline.async_wait([&](std::error_code ec) {
        if (ec) return;
        deadline_fired = true;
        input.cancel(); input.close();
        output.cancel(); output.close();
    });
    reorg_outcome aborted;
    auto coordinator = ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        try {
            // Abort before mutation: pause ends, but the generation does not
            // change. Dropping the received chunk here would lose real work.
            aborted = co_await execute_reorg(chain, fixture.organizer(), idx, 2,
                [] { return true; }, real_persister(chain), reorg_pipeline{&input, &contiguous});
            auto received = co_await output.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if ( ! std::get<0>(received)) {
                ack = std::move(std::get<1>(received));
                ack_received = true;
            }
        } catch (...) {
            deadline.cancel();
            input.cancel(); input.close();
            output.cancel(); output.close();
            throw;
        }
        deadline.cancel();
        input.cancel(); input.close();
    }, ::asio::use_future);
    ctx.run();
    coordinator.get();
    storage.get();
    CHECK(accepted);
    CHECK(parked == std::make_pair(size_t{1}, size_t{1}));
    CHECK_FALSE(stored_before_resume);
    CHECK_FALSE(deadline_fired);
    CHECK_FALSE(aborted.result.ok);
    CHECK_FALSE(aborted.fatal);
    CHECK(chain.chain_generation() == generation);
    CHECK(ack_received);
    CHECK(ack.start_height == 3);
    CHECK(ack.block_count == 1);
    CHECK(ack.result == error::success);
    CHECK(chain.headers().has_status(idx, blockchain::header_status::have_data));
    CHECK(contiguous.load() == 4);
    CHECK_FALSE(output.try_receive([](std::error_code, chunk_validated) {}));
    CHECK(chain.reorg_barrier_state() == std::make_pair(size_t{0}, size_t{0}));
}
