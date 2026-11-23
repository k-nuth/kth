// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

// =============================================================================
// The UTXO build waits to be told, and holds no timer
// =============================================================================
//
// With nothing to build the task waits on its wake channel. These drive it
// alone on a context that is only ever polled, so no time passes between a
// ring and the look at what it did: a task that woke on a timer instead would
// not have moved.

namespace {

// Everything ready on the context, and nothing that needs time to pass.
void settle(::asio::io_context& ctx) {
    ctx.restart();
    ctx.poll();
}

} // namespace

TEST_CASE("the UTXO build wakes when it is rung, and only then", "[node][utxo][wake]") {
    chain_fixture fixture("utxo_build_wake_idle");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    ::asio::io_context ctx;
    std::atomic<uint32_t> contiguous{1};        // nothing stored past the genesis
    utxo_build_wake_channel wake(ctx.get_executor(), 1);
    int passes = 0;
    bool stop = false;
    std::vector<std::string> fatals;

    auto builder = ::asio::co_spawn(ctx,
        utxo_build_task(fixture.chain(), contiguous, wake, 1, domain::config::network::regtest,
            [&] { ++passes; return stop; },
            [&fatals](std::string const& reason) { fatals.push_back(reason); }),
        ::asio::use_future);

    settle(ctx);
    REQUIRE(passes == 1);

    // Longer than any interval it could be polling on.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    settle(ctx);
    CHECK(passes == 1);

    REQUIRE(wake.try_send(std::error_code{}));
    settle(ctx);
    CHECK(passes == 2);

    // The stop request is one more thing it is rung for.
    stop = true;
    REQUIRE(wake.try_send(std::error_code{}));
    settle(ctx);
    REQUIRE(builder.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    builder.get();
    CHECK(fatals.empty());
}

TEST_CASE("an idle UTXO build parks for a chain switch when it is rung", "[node][utxo][wake]") {
    chain_fixture fixture("utxo_build_wake_pause");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    ::asio::io_context ctx;
    std::atomic<uint32_t> contiguous{1};
    utxo_build_wake_channel wake(ctx.get_executor(), 1);
    bool stop = false;
    std::vector<std::string> fatals;

    auto builder = ::asio::co_spawn(ctx,
        utxo_build_task(chain, contiguous, wake, 1, domain::config::network::regtest,
            [&] { return stop; },
            [&fatals](std::string const& reason) { fatals.push_back(reason); }),
        ::asio::use_future);

    settle(ctx);
    REQUIRE(chain.reorg_barrier_state() == std::pair<size_t, size_t>{0, 1});

    // What a switch does: raise the pause, then ring.
    chain.request_reorg_pause(true);
    REQUIRE(wake.try_send(std::error_code{}));
    settle(ctx);
    CHECK(chain.reorg_barrier_reached());
    CHECK(chain.reorg_barrier_state() == std::pair<size_t, size_t>{1, 1});

    // And on the way out: lower it, then ring.
    chain.request_reorg_pause(false);
    REQUIRE(wake.try_send(std::error_code{}));
    settle(ctx);
    CHECK(chain.reorg_barrier_state() == std::pair<size_t, size_t>{0, 1});

    // A closed channel ends the task wherever it is waiting.
    wake.close();
    settle(ctx);
    REQUIRE(builder.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    builder.get();
    CHECK(chain.reorg_barrier_state() == std::pair<size_t, size_t>{0, 0});
    CHECK(fatals.empty());
}
