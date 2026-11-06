// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <chrono>
#include <future>
#include <asio/co_spawn.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::test;

TEST_CASE("block submission without a node admission owner refuses promptly",
          "[block_submission][standalone]") {
    chain_fixture fixture("submission_without_node");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    ::asio::io_context io;
    // The base control used organize(block); submit_block is its replacement
    // at the same boundary, with the same block and observation.
    auto result = ::asio::co_spawn(io, fixture.chain().submit_block(std::move(block)), ::asio::use_future);
    io.run();
    CHECK(result.get().error == error::service_stopped);
}

TEST_CASE("block submission queued before its consumer starts is released by chain stop",
          "[block_submission][shutdown]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_stop_queued");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, fixture.chain().submit_block(block), ::asio::use_future);
    io.poll();
    CHECK(submissions->buffered() == 1);
    CHECK(result.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    REQUIRE(fixture.chain().stop());
    io.restart();
    io.run();
    CHECK(result.get().error == error::service_stopped);
    CHECK(submissions->buffered() == 0);
}

TEST_CASE("block submission under admission pressure refuses without discarding admitted work",
          "[block_submission][backpressure]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_pressure");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    ::asio::io_context io;
    auto first = ::asio::co_spawn(io, fixture.chain().submit_block(block), ::asio::use_future);
    io.poll();
    REQUIRE(submissions->buffered() == 1);
    auto second = ::asio::co_spawn(io, fixture.chain().submit_block(block), ::asio::use_future);
    io.poll();
    REQUIRE(second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(second.get().error == error::operation_failed);
    CHECK(submissions->buffered() == 1);
    REQUIRE(fixture.chain().stop());
    io.restart();
    io.run();
    CHECK(first.get().error == error::service_stopped);
}

TEST_CASE("cancelling a block submitter does not withdraw work already admitted",
          "[block_submission][cancellation]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_cancel_waiter");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    ::asio::io_context io;
    ::asio::cancellation_signal cancel;
    auto result = ::asio::co_spawn(io, fixture.chain().submit_block(block),
        ::asio::bind_cancellation_slot(cancel.slot(), ::asio::use_future));
    io.poll();
    REQUIRE(submissions->buffered() == 1);
    cancel.emit(::asio::cancellation_type::terminal);
    io.restart();
    io.run();
    CHECK(result.get().error == error::service_stopped);
    std::error_code ec;
    auto pending = submissions->try_receive(ec);
    REQUIRE(pending.has_value());
    CHECK(pending->block == block);
    CHECK(pending->reply->try_send(std::error_code{}, code(error::success)));
}

TEST_CASE("cancellation before a block submission starts admits no block",
          "[block_submission][cancellation]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_cancel_before_start");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    ::asio::io_context io;
    ::asio::cancellation_signal cancel;
    auto before_start = [&]() -> ::asio::awaitable<code> {
        co_await ::asio::this_coro::throw_if_cancelled(false);
        // Deliver on the operation's executor before starting submission.
        // Emitting from outside io.run() only schedules cancellation ingress;
        // it does not prove cancellation has reached the coroutine yet.
        cancel.emit(::asio::cancellation_type::terminal);
        co_return (co_await fixture.chain().submit_block(block)).error;
    };
    auto result = ::asio::co_spawn(io, before_start(),
        ::asio::bind_cancellation_slot(cancel.slot(), ::asio::use_future));
    io.run();
    CHECK(submissions->buffered() == 0);
    try {
        CHECK(result.get() != error::success);
    } catch (::asio::system_error const& ec) {
        CHECK(ec.code() == ::asio::error::operation_aborted);
    }
}

TEST_CASE("a lazy block submission owns its endpoint after chain destruction",
          "[block_submission][lifetime]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_lazy_lifetime");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    auto block = std::make_shared<domain::message::block const>(
        domain::message::block(domain::chain::block::genesis_regtest()));
    auto operation = fixture.chain().submit_block(block);
    fixture.close();
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::move(operation), ::asio::use_future);
    io.run();
    CHECK(result.get().error == error::service_stopped);
    CHECK(submissions->buffered() == 0);
}

TEST_CASE("one block submission preserves validity and duplication independently",
          "[block_submission][result]") {
    auto submissions = std::make_shared<blockchain::block_submission_channel>(1, 1);
    chain_fixture fixture("submission_complete_reply");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start(submissions));
    using blockchain::block_submission_result;
    using blockchain::block_validity;
    for (auto const expected : {
             block_submission_result{error::success, block_validity::valid, false},
             block_submission_result{error::duplicate_block, block_validity::valid, true},
             block_submission_result{error::coinbase_value_limit, block_validity::invalid, false},
             block_submission_result{error::duplicate_block, block_validity::invalid, true},
             block_submission_result{error::duplicate_block, block_validity::unknown, true},
             block_submission_result{error::operation_failed, block_validity::unknown, false}}) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io,
            fixture.chain().submit_block(domain::chain::block::genesis_regtest()), ::asio::use_future);
        io.poll();
        std::error_code ec;
        auto pending = submissions->try_receive(ec);
        REQUIRE_FALSE(ec);
        REQUIRE(pending.has_value());
        REQUIRE(pending->reply->try_send(std::error_code{}, expected));
        io.restart();
        io.run();
        auto const actual = result.get();
        CHECK(actual.error == expected.error);
        CHECK(actual.validity == expected.validity);
        CHECK(actual.duplicate == expected.duplicate);
    }
}
