// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/utility/timer.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <kth/blockchain/pools/block_reader.hpp>
#include "reorg_chain_fixture.hpp"
#include "regtest_miner.hpp"

using namespace kth;

TEST_CASE("block reader closes queued and pending admission before its owner starts", "[block reader]") {
    // No disk owner yet: the first request fills the queue, the next holds the
    // one bounded pending send, and the third is explicitly refused.
    blockchain::block_reader reader({}, {}, {1, 0, 0});
    ::asio::io_context io;
    auto first = ::asio::co_spawn(io, reader.fetch_block(null_hash, 0, {}), ::asio::use_future);
    auto second = ::asio::co_spawn(io, reader.fetch_block(null_hash, 0, {}), ::asio::use_future);
    auto third = ::asio::co_spawn(io, reader.fetch_block(null_hash, 0, {}), ::asio::use_future);
    io.poll();
    REQUIRE(third.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    auto refused = third.get();
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == error::oversubscribed);
    reader.stop();
    io.restart(); io.run();
    for (auto* pending : {&first, &second}) {
        auto result = pending->get();
        REQUIRE_FALSE(result);
        CHECK(result.error() == error::service_stopped);
    }
}

TEST_CASE("block reader cancellation withdraws an admitted reply", "[block reader]") {
    blockchain::block_reader reader({}, {}, {1, 0, 0});
    ::asio::io_context io;
    ::asio::cancellation_signal signal;
    auto pending = ::asio::co_spawn(io, reader.fetch_block(null_hash, 0, {}),
        ::asio::bind_cancellation_slot(signal.slot(), ::asio::use_future));
    io.poll(); // operation admitted; no disk task exists to answer it
    ::asio::post(io, [&] { signal.emit(::asio::cancellation_type::terminal); });
    io.restart(); io.run();
    auto result = pending.get();
    REQUIRE_FALSE(result);
    CHECK(result.error() == error::channel_stopped);
    reader.start(); // skips the cancelled debt; stop joins that owner
    reader.stop();
}

TEST_CASE("a lazy block read retains only a closed endpoint after destruction", "[block reader]") {
    auto reader = std::make_unique<blockchain::block_reader>(
        std::filesystem::path{}, database::block_store::magic_t{}, blockchain::block_reader::limits{1, 0, 0});
    auto operation = reader->fetch_block(null_hash, 0, {});
    reader.reset();
    ::asio::io_context io;
    auto pending = ::asio::co_spawn(io, std::move(operation), ::asio::use_future);
    io.run();
    auto result = pending.get();
    REQUIRE_FALSE(result);
    CHECK(result.error() == error::service_stopped);
}

TEST_CASE("block reader cancellation is observed before admission", "[block reader]") {
    blockchain::block_reader reader({}, {}, {1, 0, 0});
    ::asio::io_context io;
    ::asio::cancellation_signal signal;
    auto scenario = [&]() -> ::asio::awaitable<blockchain::block_reader::result> {
        co_await ::asio::this_coro::throw_if_cancelled(false);
        signal.emit(::asio::cancellation_type::terminal);
        co_return co_await reader.fetch_block(null_hash, 0, {});
    };
    auto pending = ::asio::co_spawn(io, scenario(),
        ::asio::bind_cancellation_slot(signal.slot(), ::asio::use_future));
    io.run();
    auto result = pending.get();
    REQUIRE_FALSE(result);
    CHECK(result.error() == error::channel_stopped);
}

TEST_CASE("block reader cache hits preserve immutable blocks and bounded LRU eviction", "[block reader]") {
    test::chain_fixture fixture("block_reader_cache");
    REQUIRE(fixture.created()); REQUIRE(fixture.start());
    auto const a = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 1200, 0, {}, 0);
    auto const b = test::mine_block(a.hash(), 2, a.header().timestamp() + 600, 0, {}, 0);
    auto const path = fixture.chain().data_dir() / "blocks";
    auto const magic = to_little_endian(get_disk_magic(domain::config::network::regtest));
    database::block_store writer(path, magic);
    REQUIRE(writer.initialize());
    REQUIRE(writer.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {}).clean());
    REQUIRE(writer.scan_undo_positions([](hash_digest const&) -> std::optional<hash_digest> {
        return std::nullopt;
    }).status == database::block_store::undo_scan_status::clean_eof);
    auto const pos_a = writer.save_block(a, 1);
    auto const pos_b = writer.save_block(b, 2);
    REQUIRE_FALSE(pos_a.is_null()); REQUIRE_FALSE(pos_b.is_null());
    blockchain::block_reader::limits budget{2, 1, max_size_t};
    bool cached = true;
    SECTION("entry bound") {}
    SECTION("serialized byte bound") {
        budget.cache_entries = 8;
        budget.cache_bytes = std::max(a.serialized_size(), b.serialized_size());
    }
    SECTION("zero byte budget disables caching") { budget.cache_bytes = 0; cached = false; }
    blockchain::block_reader reader(path, magic, budget);
    reader.start();
    auto fetch = [&](hash_digest hash, size_t height, database::flat_file_pos pos) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, reader.fetch_block(hash, height, pos), ::asio::use_future);
        io.run(); return result.get();
    };
    auto const first = fetch(a.hash(), 1, pos_a);
    auto const again = fetch(a.hash(), 1, pos_a);
    REQUIRE(first); REQUIRE(again);
    CHECK(first->first->hash() == a.hash());
    CHECK(again->second == 1);
    CHECK((first->first == again->first) == cached);
    auto const other = fetch(b.hash(), 2, pos_b);
    REQUIRE(other);
    auto const evicted = fetch(a.hash(), 1, pos_a);
    REQUIRE(evicted);
    CHECK(evicted->first != first->first);
    CHECK(first->first->hash() == a.hash()); // remains owned after eviction
}
