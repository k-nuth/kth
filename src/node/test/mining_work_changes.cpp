// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/node/mining/work_changes.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using node::mining::doorbell;
using node::mining::work_changes;

namespace {

// Far more than a ring needs, and what a wait that should not end costs.
constexpr auto patience = std::chrono::seconds(5);
constexpr auto quiet = std::chrono::milliseconds(200);

// Whether `bell` rings within `limit`, from the test's thread.
bool rings_within(doorbell& bell, std::chrono::steady_clock::duration limit) {
    using namespace ::asio::experimental;
    ::asio::io_context io;
    auto done = ::asio::co_spawn(io, [&]() -> ::asio::awaitable<bool> {
        auto const executor = co_await ::asio::this_coro::executor;
        auto completion = co_await make_select(receive(bell),
            after(std::chrono::steady_clock::now() + limit)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        co_return ! ec && selected.index == 0;
    }, ::asio::use_future);
    io.run();
    return done.get();
}

// Whether `bell` has room for a ring within `limit`. A doorbell holds one, so
// one that was rung has room again only once its consumer has taken that ring.
// The ring this leaves in it is one more for the consumer.
bool room_within(doorbell& bell, std::chrono::steady_clock::duration limit) {
    using namespace ::asio::experimental;
    ::asio::io_context io;
    auto done = ::asio::co_spawn(io, [&]() -> ::asio::awaitable<bool> {
        auto const executor = co_await ::asio::this_coro::executor;
        auto completion = co_await make_select(send(bell, std::monostate{}),
            after(std::chrono::steady_clock::now() + limit)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        co_return ! ec && selected.index == 0;
    }, ::asio::use_future);
    io.run();
    return done.get();
}

// A chain with the node's work_changes running on a thread of its own.
struct watched_chain {
    explicit watched_chain(char const* tag)
        : chain(tag) {}

    ~watched_chain() {
        changes.stop();
        if (runner.joinable()) runner.join();
    }

    [[nodiscard]] bool start() {
        if ( ! chain.created() || ! chain.start()) return false;
        done = ::asio::co_spawn(io, changes.run(chain.chain()), ::asio::use_future);
        runner = std::thread([this] { io.run(); });
        return true;
    }

    // Change the mempool the way an admission does.
    void change_mempool(uint32_t tag) {
        auto const tx = std::make_shared<domain::message::transaction>(
            domain::chain::transaction{1u, tag, {}, {}});
        REQUIRE(chain.chain().mempool_ref().add(blockchain::mempool_entry{
            tx, 0u, uint32_t(tx->serialized_size(true)), 0u, 0u}));
    }

    // Change the tip's doorbell the way a finished transition does.
    void finish_transition() {
        REQUIRE(chain.chain().begin_transition());
        chain.chain().end_transition();
    }

    [[nodiscard]] bool ended_within(std::chrono::steady_clock::duration limit) const {
        return done.wait_for(limit) == std::future_status::ready;
    }

    test::chain_fixture chain;
    work_changes changes;
    ::asio::io_context io;
    std::future<void> done;
    std::thread runner;
};

} // namespace

TEST_CASE("work changes ring every subscriber when the mempool or the tip changes", "[work changes]") {
    watched_chain watched("work_changes_every_subscriber");
    auto& first = watched.changes.subscribe();
    auto& second = watched.changes.subscribe();
    REQUIRE(watched.start());

    SECTION("a change of the mempool") {
        watched.change_mempool(7u);
    }
    SECTION("the end of a transition") {
        watched.finish_transition();
    }
    CHECK(rings_within(first, patience));
    CHECK(rings_within(second, patience));
    // One change is one ring for each.
    CHECK_FALSE(rings_within(first, quiet));
    CHECK_FALSE(rings_within(second, quiet));
}

TEST_CASE("work changes a subscriber has not read are one ring, and do not hold the others back",
          "[work changes]") {
    watched_chain watched("work_changes_unread");
    auto& slow = watched.changes.subscribe();
    auto& reader = watched.changes.subscribe();
    REQUIRE(watched.start());

    // `reader` takes each ring; `slow` reads none of the three.
    for (uint32_t tag = 1; tag <= 3; ++tag) {
        watched.change_mempool(tag);
        REQUIRE(rings_within(reader, patience));
    }
    CHECK(rings_within(slow, patience));
    CHECK_FALSE(rings_within(slow, quiet));
}

TEST_CASE("work changes without subscribers still take the chain's rings", "[work changes]") {
    watched_chain watched("work_changes_no_subscribers");
    REQUIRE(watched.start());
    watched.change_mempool(1u);
    watched.finish_transition();
    // Each of the chain's doorbells was rung, and has room again only once
    // that ring was taken.
    auto& chain = watched.chain.chain();
    CHECK(room_within(chain.mempool_ref().changes(), patience));
    CHECK(room_within(chain.tip_changes(), patience));
    watched.changes.stop();
    CHECK(watched.ended_within(patience));
}

TEST_CASE("work changes end when they are stopped", "[work changes]") {
    watched_chain watched("work_changes_stop");
    auto& bell = watched.changes.subscribe();

    SECTION("while waiting for a change") {
        REQUIRE(watched.start());
        CHECK_FALSE(watched.ended_within(quiet));
        watched.changes.stop();
        CHECK(watched.ended_within(patience));
    }

    SECTION("before they run, with a change already pending") {
        REQUIRE(watched.chain.created());
        REQUIRE(watched.chain.start());
        watched.change_mempool(1u);
        watched.changes.stop();
        watched.done = ::asio::co_spawn(watched.io, watched.changes.run(watched.chain.chain()),
            ::asio::use_future);
        watched.runner = std::thread([&] { watched.io.run(); });
        CHECK(watched.ended_within(patience));
        CHECK_FALSE(rings_within(bell, quiet));
    }
}
