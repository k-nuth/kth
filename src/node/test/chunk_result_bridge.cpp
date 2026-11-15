// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/detail/chunk_result_bridge.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::node::sync;

namespace {

// Only channel completions run here. Bound the ready handlers too, so a bridge
// that spins instead of suspending fails the control rather than hanging it.
void drain(::asio::io_context& ioc) {
    ioc.restart();
    for (size_t i = 0; i < 1024; ++i) {
        if (ioc.poll_one() == 0) return;
    }
    FAIL("chunk result bridge did not stop making immediate progress");
}

void take(sync_coordinator_event_channel& events, chunk_validated const& expected) {
    std::optional<sync_coordinator_event> event;
    REQUIRE(events.try_receive([&](std::error_code ec, sync_coordinator_event value) {
        CHECK_FALSE(ec);
        event = std::move(value);
    }));
    REQUIRE(event.has_value());
    auto const* result = std::get_if<chunk_validated>(&*event);
    REQUIRE(result != nullptr);
    CHECK(result->start_height == expected.start_height);
    CHECK(result->block_count == expected.block_count);
    CHECK(result->result == expected.result);
    CHECK(result->source_peer == expected.source_peer);
}

} // namespace

TEST_CASE("chunk result bridge: a full coordinator retains every result in order",
          "[chunk_result_bridge][tasks][saturation]") {
    ::asio::io_context ioc;
    chunk_validated_channel input(ioc, 2);
    sync_coordinator_event_channel events(ioc, 1);
    chunk_validated const sentinel{0, 1, error::success, nullptr};
    std::array<chunk_validated, 4> const results{{
        {1, 2, error::success, nullptr},
        {3, 1, error::operation_failed, nullptr},
        {4, 3, error::success, nullptr},
        {7, 1, error::success, nullptr}
    }};

    REQUIRE(events.try_send(std::error_code{}, sentinel));
    REQUIRE(input.try_send(std::error_code{}, results[0]));
    REQUIRE(input.try_send(std::error_code{}, results[1]));
    auto bridge = ::asio::co_spawn(ioc,
        node::detail::chunk_result_bridge(input, events, "test bridge"), ::asio::use_future);
    drain(ioc);

    // The first result is held by the bridge; two more wait at its input.
    REQUIRE(input.try_send(std::error_code{}, results[2]));
    drain(ioc);
    CHECK(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    take(events, sentinel);
    for (size_t i = 0; i < 3; ++i) {
        drain(ioc);
        CHECK(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
        take(events, results[i]);
    }

    // The bridge still receives after the pressure has passed.
    drain(ioc);
    REQUIRE(input.try_send(std::error_code{}, results[3]));
    drain(ioc);
    take(events, results[3]);
    CHECK_FALSE(events.try_receive([](std::error_code, sync_coordinator_event) {}));
    CHECK(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);

    input.close();
    drain(ioc);
    REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    bridge.get();
}

TEST_CASE("chunk result bridge: closing an empty input ends the bridge",
          "[chunk_result_bridge][tasks][shutdown]") {
    ::asio::io_context ioc;
    chunk_validated_channel input(ioc, 1);
    sync_coordinator_event_channel events(ioc, 1);

    auto bridge = ::asio::co_spawn(ioc,
        node::detail::chunk_result_bridge(input, events, "test bridge"), ::asio::use_future);
    drain(ioc);
    REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    input.close();
    drain(ioc);
    REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    bridge.get();
    CHECK_FALSE(events.try_receive([](std::error_code, sync_coordinator_event) {}));
}

TEST_CASE("chunk result bridge: coordinator shutdown ends delivery",
          "[chunk_result_bridge][tasks][shutdown]") {
    ::asio::io_context ioc;
    chunk_validated_channel input(ioc, 1);
    sync_coordinator_event_channel events(ioc, 1);
    chunk_validated const result{1, 1, error::success, nullptr};

    SECTION("closed before delivery") {
        events.close();
        REQUIRE(input.try_send(std::error_code{}, result));
        auto bridge = ::asio::co_spawn(ioc,
            node::detail::chunk_result_bridge(input, events, "test bridge"), ::asio::use_future);
        drain(ioc);
        REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        bridge.get();
    }

    SECTION("shutdown while delivery waits for room") {
        REQUIRE(events.try_send(std::error_code{}, result));
        REQUIRE(input.try_send(std::error_code{}, result));
        auto bridge = ::asio::co_spawn(ioc,
            node::detail::chunk_result_bridge(input, events, "test bridge"), ::asio::use_future);
        drain(ioc);
        REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);

        // Match orchestrator shutdown: close alone does not wake a pending send
        // in concurrent_channel, so cancellation comes first.
        events.cancel();
        events.close();
        drain(ioc);
        REQUIRE(bridge.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        bridge.get();
    }
}
