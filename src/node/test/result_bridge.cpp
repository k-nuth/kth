// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/detail/result_bridge.hpp>
#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::detail;
using namespace kth::node::sync;

// =============================================================================
// What a stage reports reaches the coordinator, however busy the coordinator is
// =============================================================================
//
// The coordinator's channel is shared by every event it attends, and it is not
// read while the coordinator carries out a switch. These cases leave it full
// and unread, and look at what became of the results reported meanwhile.

namespace {

// Run what is ready, and return when nothing more can happen without time
// passing or an outside event.
void drain(::asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
    }
}

bool ended(std::future<void> const& task) {
    return task.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

chunk_validated chunk_at(uint32_t start_height) {
    return chunk_validated{
        .start_height = start_height, .block_count = 1, .result = error::success, .source_peer = nullptr};
}

std::optional<sync_coordinator_event> next_event(sync_coordinator_event_channel& events) {
    std::optional<sync_coordinator_event> event;
    (void)events.try_receive([&](std::error_code ec, sync_coordinator_event value) {
        if ( ! ec) event = std::move(value);
    });
    return event;
}

// The coordinator's channel with every slot taken by something else.
void fill(sync_coordinator_event_channel& events, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        REQUIRE(events.try_send(std::error_code{}, headers_owed_event{}));
    }
}

} // namespace

TEST_CASE("result bridge: results reported while the coordinator's channel is full all arrive, in order",
          "[result_bridge]") {
    constexpr size_t capacity = 2;
    constexpr uint32_t reported = 6;

    ::asio::io_context ioc;
    chunk_validated_channel input(ioc.get_executor(), 8);
    sync_coordinator_event_channel events(ioc.get_executor(), capacity);
    fill(events, capacity);

    auto task = ::asio::co_spawn(ioc, result_bridge(input, events, "test"), ::asio::use_future);
    for (uint32_t i = 0; i < reported; ++i) {
        REQUIRE(input.try_send(std::error_code{}, chunk_at(10 + i)));
    }
    drain(ioc);

    // Nothing was read from the coordinator's channel, and the bridge is still
    // there, holding the first result.
    CHECK_FALSE(ended(task));

    // The coordinator reads again: what filled its channel first, then every
    // result, none missing and none out of place.
    std::vector<uint32_t> arrived;
    size_t others = 0;
    while (true) {
        auto event = next_event(events);
        if ( ! event) {
            drain(ioc);
            event = next_event(events);
            if ( ! event) break;
        }
        if (auto const* result = std::get_if<chunk_validated>(&*event)) {
            arrived.push_back(result->start_height);
        } else {
            CHECK(arrived.empty());
            ++others;
        }
    }
    CHECK(others == capacity);
    CHECK(arrived == std::vector<uint32_t>{10, 11, 12, 13, 14, 15});
    CHECK_FALSE(ended(task));

    input.close();
    drain(ioc);
    CHECK(ended(task));
}

TEST_CASE("result bridge: it carries the results of block validation the same way",
          "[result_bridge]") {
    constexpr size_t capacity = 1;

    ::asio::io_context ioc;
    block_validated_channel input(ioc.get_executor(), 4);
    sync_coordinator_event_channel events(ioc.get_executor(), capacity);
    fill(events, capacity);

    auto task = ::asio::co_spawn(ioc, result_bridge(input, events, "test"), ::asio::use_future);
    for (uint32_t height : {7u, 8u, 9u}) {
        REQUIRE(input.try_send(std::error_code{},
            block_validated{.height = height, .result = error::success, .source_peer = nullptr}));
    }
    drain(ioc);
    CHECK_FALSE(ended(task));

    std::vector<uint32_t> arrived;
    while (true) {
        auto event = next_event(events);
        if ( ! event) {
            drain(ioc);
            event = next_event(events);
            if ( ! event) break;
        }
        if (auto const* result = std::get_if<block_validated>(&*event)) {
            arrived.push_back(result->height);
        }
    }
    CHECK(arrived == std::vector<uint32_t>{7, 8, 9});

    input.close();
    drain(ioc);
    CHECK(ended(task));
}

TEST_CASE("result bridge: the coordinator's channel closing ends a bridge that waits for room",
          "[result_bridge]") {
    constexpr size_t capacity = 1;

    ::asio::io_context ioc;
    chunk_validated_channel input(ioc.get_executor(), 4);
    sync_coordinator_event_channel events(ioc.get_executor(), capacity);
    fill(events, capacity);

    auto task = ::asio::co_spawn(ioc, result_bridge(input, events, "test"), ::asio::use_future);
    REQUIRE(input.try_send(std::error_code{}, chunk_at(10)));
    drain(ioc);
    REQUIRE_FALSE(ended(task));

    // The coordinator leaves the way it does at shutdown.
    events.cancel();
    events.close();
    drain(ioc);
    CHECK(ended(task));
}

TEST_CASE("result bridge: its input closing ends a bridge with nothing to carry",
          "[result_bridge]") {
    ::asio::io_context ioc;
    chunk_validated_channel input(ioc.get_executor(), 4);
    sync_coordinator_event_channel events(ioc.get_executor(), 4);

    auto task = ::asio::co_spawn(ioc, result_bridge(input, events, "test"), ::asio::use_future);
    drain(ioc);
    REQUIRE_FALSE(ended(task));

    input.close();
    drain(ioc);
    CHECK(ended(task));
    CHECK_FALSE(next_event(events).has_value());
}

// =============================================================================
// A stage does not drop what it reports either
// =============================================================================

TEST_CASE("block validation: a result that finds its output full waits for room",
          "[result_bridge][block_validation]") {
    test::chain_fixture fixture("validation_waits");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    ::asio::io_context ioc;
    block_validation_input_channel input(ioc.get_executor(), 4);
    block_validated_channel output(ioc.get_executor(), 1);

    // The output's one slot is taken by an earlier result nobody has read.
    REQUIRE(output.try_send(std::error_code{},
        block_validated{.height = 0, .result = error::success, .source_peer = nullptr}));

    auto task = ::asio::co_spawn(ioc,
        block_validation_task(fixture.chain(), input, output, 1, 1), ::asio::use_future);

    // The block at height 1. Whether it validates is not the point: either
    // way its result is the coordinator's to hear.
    REQUIRE(input.try_send(std::error_code{}, block_validation_input{downloaded_light_block{
        .height = 1,
        .block = std::make_shared<domain::chain::light_block const>(
            domain::chain::header{}, data_chunk{}, std::vector<uint32_t>{}),
        .source_peer = nullptr,
        .active_peers = 1}}));

    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK_FALSE(ended(task));

    // Room appears, and the result for height 1 is there to be read.
    std::vector<uint32_t> heights;
    auto const take = [&] {
        return output.try_receive([&](std::error_code ec, block_validated result) {
            if ( ! ec) heights.push_back(result.height);
        });
    };
    REQUIRE(take());
    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK(take());
    CHECK(heights == std::vector<uint32_t>{0, 1});

    input.cancel();
    input.close();
    output.cancel();
    output.close();
    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK(ended(task));
}

namespace {

// One blank body at `height`, as a peer's chunk for the chain as it is now.
downloaded_chunk peer_chunk(uint32_t height, uint64_t generation) {
    std::vector<std::shared_ptr<domain::chain::light_block const>> blocks;
    blocks.push_back(std::make_shared<domain::chain::light_block const>(
        domain::chain::header{}, data_chunk{}, std::vector<uint32_t>{}));
    return downloaded_chunk{
        .start_height = height,
        .chunk_id = 0,
        .blocks = std::move(blocks),
        .source_peer = nullptr,
        .generation = generation};
}

// The height of the next outcome `output` holds, if it holds one.
std::optional<uint32_t> take_height(chunk_validated_channel& output) {
    std::optional<uint32_t> height;
    (void)output.try_receive([&](std::error_code ec, chunk_validated result) {
        if ( ! ec) height = result.start_height;
    });
    return height;
}

} // namespace

TEST_CASE("fast validation: a verdict that finds its output full waits for room",
          "[result_bridge][fast_validation]") {
    test::chain_fixture fixture("fast_validation_waits");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    ::asio::io_context ioc;
    fast_validation_input_channel input(4);
    chunk_validated_channel output(ioc.get_executor(), 1);

    // The output's one slot is taken by an earlier outcome nobody has read.
    REQUIRE(output.try_send(std::error_code{}, chunk_at(0)));

    // No storage: every verdict, whatever it says, goes to the output.
    auto task = ::asio::co_spawn(ioc,
        fast_validation_task(chain, input, output, nullptr), ::asio::use_future);
    REQUIRE(input.try_send(fast_validation_input{peer_chunk(1, chain.chain_generation())}));

    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK_FALSE(ended(task));

    // Room appears, and the verdict for height 1 is there to be read.
    CHECK(take_height(output) == 0u);
    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK(take_height(output) == 1u);
    CHECK_FALSE(ended(task));

    input.close();
    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK(ended(task));
}

TEST_CASE("block storage: an outcome that finds its output full waits for room, parked at the barrier",
          "[result_bridge][block_storage][reorg]") {
    test::chain_fixture fixture("storage_waits");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    ::asio::io_context ioc;
    block_storage_input_channel input(ioc.get_executor(), 4);
    chunk_validated_channel output(ioc.get_executor(), 1);
    std::atomic<uint32_t> contiguous{1};

    // The output's one slot is taken by an earlier outcome nobody has read.
    REQUIRE(output.try_send(std::error_code{}, chunk_at(0)));

    auto task = ::asio::co_spawn(ioc,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::use_future);
    REQUIRE(input.try_send(std::error_code{}, peer_chunk(1, chain.chain_generation())));

    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK_FALSE(ended(task));

    // Its outcome is still owed, and nothing of the chain is being touched
    // meanwhile: a switch asking for the barrier finds this writer parked
    // instead of waiting for the room only its own end would free.
    chain.request_reorg_pause(true);
    CHECK(chain.reorg_barrier_reached());

    // Room appears while the switch is still under way. The outcome is
    // delivered, and the task stays parked until the switch is over.
    CHECK(take_height(output) == 0u);
    ioc.restart();
    ioc.run_for(std::chrono::milliseconds(200));
    CHECK(take_height(output) == 1u);
    CHECK(chain.reorg_barrier_reached());
    CHECK_FALSE(ended(task));

    chain.request_reorg_pause(false);
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ioc.restart();
    ioc.run_for(std::chrono::seconds(2));
    CHECK(ended(task));
    CHECK(chain.reorg_barrier_state() == std::make_pair(size_t{0}, size_t{0}));
}
