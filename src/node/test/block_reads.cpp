// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <cstdio>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <fmt/format.h>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;

namespace {

template <typename Awaitable>
auto run_on_own_context(Awaitable&& operation) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::forward<Awaitable>(operation), ::asio::use_future);
    io.run();
    return result.get();
}

data_chunk serialized(domain::chain::block const& block) {
    data_chunk raw(block.serialized_size());
    byte_writer writer(raw);
    REQUIRE(block.to_data(writer));
    return raw;
}

// Overwrite bytes of a block's record in its block file, relative to the start
// of the block itself. The record opens with an 8-byte header: the network
// magic, then the block's size.
void overwrite(blockchain::block_chain& chain, hash_digest const& hash, long offset, data_chunk const& bytes) {
    auto const idx = chain.headers().find(hash);
    REQUIRE(idx != blockchain::header_index::null_index);
    auto const pos = chain.headers().get_block_pos(idx);
    REQUIRE_FALSE(pos.is_null());
    auto const path = chain.data_dir() / "blocks" / fmt::format("blk{:05d}.dat", pos.file);
    FILE* file = std::fopen(path.string().c_str(), "r+b");
    REQUIRE(file != nullptr);
    REQUIRE(std::fseek(file, long(pos.pos) + offset, SEEK_SET) == 0);
    REQUIRE(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    REQUIRE(std::fclose(file) == 0);
}

} // namespace

TEST_CASE("fetch_block reads stored blocks by height and by hash, side branches included", "[block reads]") {
    test::chain_fixture fixture("block_reads");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 4; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    // A one-block side branch off height 2, stored but never connected.
    auto const side = test::mine_block(trunk[1].hash(), 3, base_time + 3 * test::block_spacing + 1, 7, {}, 0);

    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    (void)fixture.organizer().add_headers(test::headers_of(std::vector{side}));
    // Stored before the trunk connects, so the connection's write barrier puts
    // its bytes on disk too.
    REQUIRE(run_on_own_context(chain.store_block(test::to_light(side), 3)) == error::success);
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);
    REQUIRE(chain.chain_view()->connected_tip_height == 4u);

    for (uint32_t h = 1; h <= 4; ++h) {
        CAPTURE(h);
        auto const& expected = trunk[h - 1];
        auto const by_height = run_on_own_context(chain.fetch_block(size_t(h)));
        REQUIRE(by_height);
        CHECK(by_height->second == h);
        CHECK(serialized(*by_height->first) == serialized(expected));

        auto const by_hash = run_on_own_context(chain.fetch_block(expected.hash()));
        REQUIRE(by_hash);
        CHECK(by_hash->second == h);
        CHECK(by_hash->first->hash() == expected.hash());
    }

    // By hash, the side block with its own height; by height, the active one.
    auto const side_read = run_on_own_context(chain.fetch_block(side.hash()));
    REQUIRE(side_read);
    CHECK(side_read->second == 3u);
    CHECK(serialized(*side_read->first) == serialized(side));
    auto const active_three = run_on_own_context(chain.fetch_block(size_t(3)));
    REQUIRE(active_three);
    CHECK(active_three->first->hash() == trunk[2].hash());

    // What the node does not hold is not_found.
    hash_digest unknown{};
    unknown[0] = 0x42;
    for (auto const& missing : {run_on_own_context(chain.fetch_block(unknown)),
                                run_on_own_context(chain.fetch_block(size_t(5))),
                                run_on_own_context(chain.fetch_block(size_t(max_uint32)))}) {
        REQUIRE_FALSE(missing);
        CHECK(missing.error() == error::not_found);
    }
}

TEST_CASE("fetch_block reads on the chain pool, not on the caller's executor", "[block reads]") {
    test::chain_fixture fixture("block_reads_pool");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 10u * test::block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> const one{block};
    REQUIRE(fixture.organizer().add_headers(test::headers_of(one)).headers_added == 1);
    test::persist_headers(fixture, one, 1);
    test::connect_bodies(fixture, one, 1);

    // The caller's context is free while the read is in flight: a handler
    // queued right behind the read runs before the read completes. Were the
    // read done inline on the caller's executor, it would already be over.
    ::asio::io_context io;
    bool read_done = false;
    bool done_when_other_work_ran = true;
    auto read = [&]() -> ::asio::awaitable<bool> {
        auto const result = co_await chain.fetch_block(block.hash());
        read_done = true;
        co_return bool(result);
    };
    auto pending = ::asio::co_spawn(io, read(), ::asio::use_future);
    ::asio::post(io, [&] { done_when_other_work_ran = read_done; });
    io.run();
    CHECK(pending.get());
    CHECK_FALSE(done_when_other_work_ran);
}

TEST_CASE("fetch_block reports a damaged record as a failure, never as another block", "[block reads]") {
    test::chain_fixture fixture("block_reads_damaged");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);
    for (auto const& block : trunk) {
        REQUIRE(run_on_own_context(chain.fetch_block(block.hash())));
    }
    auto const failed = [&](hash_digest const& hash) {
        auto const result = run_on_own_context(chain.fetch_block(hash));
        return ! result && result.error() == error::operation_failed;
    };

    // The record cannot be read: its magic is gone.
    overwrite(chain, trunk[0].hash(), -8, data_chunk(4, 0x00));
    CHECK(failed(trunk[0].hash()));

    // It reads but does not parse: the transaction count claims far more than follows.
    overwrite(chain, trunk[1].hash(), 80, data_chunk{0xfe, 0xff, 0xff, 0xff, 0x00});
    CHECK(failed(trunk[1].hash()));

    // It parses, but the header is not the one the index names.
    overwrite(chain, trunk[2].hash(), 76, data_chunk{0x01, 0x02, 0x03, 0x04});
    CHECK(failed(trunk[2].hash()));

    // The header is right, but the coinbase no longer matches its merkle root.
    auto const coinbase_offset = long(80 + 1 + 4 + 1 + 32 + 4 + 1);
    overwrite(chain, trunk[3].hash(), coinbase_offset, data_chunk{0x5a});
    CHECK(failed(trunk[3].hash()));

    // The record claims 4 GiB, far more than its file holds: refused before
    // anything is allocated for it.
    overwrite(chain, trunk[4].hash(), -4, data_chunk(4, 0xff));
    CHECK(failed(trunk[4].hash()));
}

TEST_CASE("fetch_transaction answers a mempool transaction by txid unless confirmation is required", "[block reads]") {
    test::chain_fixture fixture("block_reads_mempool");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    domain::chain::input::list ins;
    hash_digest prevout{};
    prevout[0] = 0x42;
    ins.emplace_back(domain::chain::output_point{prevout, 0}, domain::chain::script{}, 0xffffffffu);
    domain::chain::output::list outs;
    outs.emplace_back(1000u, domain::chain::script{}, domain::chain::token_data_opt{});
    auto const pooled = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, 0u, std::move(ins), std::move(outs)});
    REQUIRE(chain.mempool_ref().add(blockchain::mempool_entry{
        pooled, 500u, uint32_t(pooled->serialized_size(true)), 1u, 0u}));

    auto const found = run_on_own_context(chain.fetch_transaction(pooled->hash(), /*require_confirmed*/ false));
    REQUIRE(found);
    CHECK(std::get<0>(*found)->hash() == pooled->hash());
    CHECK(std::get<1>(*found) == max_size_t);
    CHECK(std::get<2>(*found) == max_size_t);
    auto const position = run_on_own_context(chain.fetch_transaction_position(pooled->hash(), false));
    REQUIRE(position);
    CHECK(position->first == max_size_t);
    CHECK(position->second == max_size_t);

    auto const confirmed = run_on_own_context(chain.fetch_transaction(pooled->hash(), /*require_confirmed*/ true));
    REQUIRE_FALSE(confirmed);
    CHECK(confirmed.error() == error::not_found);
    auto const confirmed_position = run_on_own_context(chain.fetch_transaction_position(pooled->hash(), true));
    REQUIRE_FALSE(confirmed_position);
    CHECK(confirmed_position.error() == error::not_found);

    hash_digest unknown{};
    unknown[0] = 0x24;
    auto const missing = run_on_own_context(chain.fetch_transaction(unknown, false));
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == error::not_found);
}
