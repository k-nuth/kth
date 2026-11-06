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
#include "regtest_miner.hpp"

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

namespace {
auto read_rejections(blockchain::block_chain const& chain) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, chain.rejected_blocks(), ::asio::use_future);
    io.run();
    return result.get();
}

code record_rejection(blockchain::block_chain& chain, hash_digest const& hash) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, chain.record_rejected_block(hash), ::asio::use_future);
    io.run();
    return result.get();
}

// With every production handle closed, modify the on-disk table like an older
// writer or damaged record would. RAII closes all handles even on assertion failure.
void change_rejection_table(std::filesystem::path const& directory, bool remove_table) {
    MDB_env* raw;
    REQUIRE(mdb_env_create(&raw) == MDB_SUCCESS);
    std::unique_ptr<MDB_env, decltype(&mdb_env_close)> env(raw, mdb_env_close);
    REQUIRE(mdb_env_set_maxdbs(raw, 4) == MDB_SUCCESS);
    REQUIRE(mdb_env_open(raw, (directory / "internal_db").string().c_str(), 0, 0664) == MDB_SUCCESS);
    MDB_txn* txn_raw;
    REQUIRE(mdb_txn_begin(raw, nullptr, 0, &txn_raw) == MDB_SUCCESS);
    std::unique_ptr<MDB_txn, decltype(&mdb_txn_abort)> txn(txn_raw, mdb_txn_abort);
    MDB_dbi dbi;
    REQUIRE(mdb_dbi_open(txn_raw, "rejected_blocks", 0, &dbi) == MDB_SUCCESS);
    if (remove_table) {
        REQUIRE(mdb_drop(txn_raw, dbi, 1) == MDB_SUCCESS);
    } else {
        hash_digest hash{};
        uint8_t unsupported_version = 2;
        auto key = kth_db_make_value(hash.size(), hash.data());
        auto value = kth_db_make_value(1, &unsupported_version);
        REQUIRE(mdb_put(txn_raw, dbi, &key, &value, 0) == MDB_SUCCESS);
    }
    REQUIRE(mdb_txn_commit(txn.release()) == MDB_SUCCESS);
}
}

TEST_CASE("rejection history survives reopen and recording is idempotent", "[block_submission][rejection_store]") {
    chain_fixture fixture("rejection_store");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const empty = read_rejections(fixture.chain());
    REQUIRE(empty);
    REQUIRE(empty->empty());
    hash_digest rejected{};
    rejected.front() = 19;
    REQUIRE(record_rejection(fixture.chain(), rejected) == error::success);
    REQUIRE(record_rejection(fixture.chain(), rejected) == error::success);
    REQUIRE(fixture.restart());
    auto const restored = read_rejections(fixture.chain());
    REQUIRE(restored);
    REQUIRE(restored->size() == 1);
    CHECK(restored->front() == rejected);
}

TEST_CASE("an older database without rejection history opens with an empty history", "[block_submission][rejection_store]") {
    chain_fixture fixture("rejection_store_upgrade");
    REQUIRE(fixture.created());
    fixture.close();
    change_rejection_table(fixture.dir(), true);
    REQUIRE(fixture.start());
    auto const restored = read_rejections(fixture.chain());
    REQUIRE(restored);
    CHECK(restored->empty());
}

TEST_CASE("damaged rejection history is refused rather than forgotten", "[block_submission][rejection_store]") {
    chain_fixture fixture("rejection_store_corrupt");
    REQUIRE(fixture.created());
    fixture.close();
    change_rejection_table(fixture.dir(), false);
    CHECK_FALSE(fixture.start());
}

TEST_CASE("startup recovers active headers containing a committed rejection", "[block_submission][rejection_store][rejection_recovery]") {
    chain_fixture fixture("rejection_store_interrupted");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const genesis = domain::chain::block::genesis_regtest();
    constexpr uint32_t spacing = 120;
    auto const one = mine_block(genesis.hash(), 1, genesis.header().timestamp() + spacing, 0, {}, 0);
    auto const invalid = mine_block(one.hash(), 2, genesis.header().timestamp() + 2 * spacing, 0, {}, 1);
    auto const child = mine_block(invalid.hash(), 3, genesis.header().timestamp() + 3 * spacing, 0, {}, 0);
    domain::message::header::list headers{one.header(), invalid.header(), child.header()};
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == 3u);
    REQUIRE_FALSE(fixture.chain().organize_headers_batch({one.header(), invalid.header(), child.header()}, 1));
    // The verdict is durable, but a crash prevented truncation. No rejected
    // block is connected: only the header table contains this suffix.
    REQUIRE(record_rejection(fixture.chain(), invalid.hash()) == error::success);
    REQUIRE(fixture.restart());
    auto& index = fixture.chain().headers();
    auto const invalid_idx = index.find(invalid.hash());
    auto const child_idx = index.find(child.hash());
    REQUIRE(invalid_idx != blockchain::header_index::null_index);
    REQUIRE(child_idx != blockchain::header_index::null_index);
    CHECK(index.has_status(invalid_idx, blockchain::header_status::failed_valid));
    CHECK(index.has_status(child_idx, blockchain::header_status::failed_child));
    CHECK(index.get_hash(fixture.organizer().tip_index()) == one.hash());
    auto const heights = fixture.chain().get_last_heights();
    REQUIRE(heights);
    CHECK(heights->header == 1);
    CHECK(heights->block == 0);
    REQUIRE(fixture.chain().get_header(0));
    CHECK(domain::chain::hash(*fixture.chain().get_header(0)) == genesis.hash());
    CHECK(fixture.organizer().add_headers({invalid.header(), child.header()}).error);
    auto const replacement = mine_block(one.hash(), 2, genesis.header().timestamp() + 2 * spacing, 19, {}, 0);
    REQUIRE(fixture.organizer().add_headers({replacement.header()}).headers_added == 1);
    REQUIRE_FALSE(fixture.chain().organize_headers_batch({replacement.header()}, 2));
    REQUIRE(fixture.restart());
    CHECK(fixture.chain().headers().get_hash(fixture.organizer().tip_index()) == replacement.hash());
}

TEST_CASE("restart rejects peer replays of a rejected header absent from the height table", "[block_submission][rejection_store][rejection_recovery]") {
    chain_fixture fixture("rejection_store_peer_replay");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const genesis = domain::chain::block::genesis_regtest();
    constexpr uint32_t spacing = 120;
    auto const invalid = mine_block(genesis.hash(), 1, genesis.header().timestamp() + spacing, 0, {}, 1);
    auto const child = mine_block(invalid.hash(), 2, genesis.header().timestamp() + 2 * spacing, 0, {}, 0);
    REQUIRE(record_rejection(fixture.chain(), invalid.hash()) == error::success);
    REQUIRE(fixture.restart());
    // add_headers is the peer header admission boundary. The rejected hash is
    // deliberately absent from the reconstructed by-height index.
    REQUIRE(fixture.chain().headers().find(invalid.hash()) == blockchain::header_index::null_index);
    auto const replay = fixture.organizer().add_headers({invalid.header(), child.header()});
    CHECK(replay.error);
    CHECK(replay.headers_added == 0);
    CHECK_FALSE(replay.reorg_candidate);
    CHECK(fixture.organizer().header_height() == 0);
    auto const descendant = fixture.organizer().add_headers({child.header()});
    CHECK(descendant.error);
    CHECK(descendant.headers_added == 0);
    REQUIRE(fixture.restart());
    CHECK(fixture.organizer().header_height() == 0);
    auto const replacement = mine_block(genesis.hash(), 1, genesis.header().timestamp() + spacing, 19, {}, 0);
    REQUIRE(fixture.organizer().add_headers({replacement.header()}).headers_added == 1);
    REQUIRE_FALSE(fixture.chain().organize_headers_batch({replacement.header()}, 1));
    REQUIRE(fixture.restart());
    CHECK(fixture.chain().headers().get_hash(fixture.organizer().tip_index()) == replacement.hash());
}
