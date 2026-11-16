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

TEST_CASE("a new store holds the genesis block and reads it like any other", "[block reads][genesis]") {
    test::chain_fixture fixture("block_reads_genesis");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const genesis = domain::chain::block::genesis_regtest();

    auto const idx = fixture.chain().headers().active_at(0);
    REQUIRE(idx != blockchain::header_index::null_index);
    REQUIRE(fixture.chain().headers().get_hash(idx) == genesis.hash());
    CHECK(fixture.chain().headers().has_block_data(idx));
    // Never connected, so it has no undo record.
    CHECK_FALSE(fixture.chain().headers().has_undo_data(idx));
    auto const written = fixture.chain().headers().get_block_pos(idx);

    auto const by_height = run_on_own_context(fixture.chain().fetch_block(size_t(0)));
    REQUIRE(by_height);
    CHECK(by_height->second == 0u);
    CHECK(serialized(*by_height->first) == serialized(genesis));
    auto const by_hash = run_on_own_context(fixture.chain().fetch_block(genesis.hash()));
    REQUIRE(by_hash);
    CHECK(by_hash->second == 0u);
    CHECK(serialized(*by_hash->first) == serialized(genesis));

    // A restart finds it where it was written and does not write it again.
    REQUIRE(fixture.restart());
    auto const reopened = fixture.chain().headers().find(genesis.hash());
    REQUIRE(reopened != blockchain::header_index::null_index);
    auto const found = fixture.chain().headers().get_block_pos(reopened);
    CHECK(found.file == written.file);
    CHECK(found.pos == written.pos);
    auto const again = run_on_own_context(fixture.chain().fetch_block(size_t(0)));
    REQUIRE(again);
    CHECK(serialized(*again->first) == serialized(genesis));
}

TEST_CASE("a store created without the genesis block gets it at start", "[block reads][genesis]") {
    // A database created before the genesis block was stored: its header in the
    // database, and the block files holding every block but that one.
    test::chain_fixture fixture("block_reads_genesis_missing");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 2; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);

    // The genesis record opens the first block file, and the trunk follows it.
    auto const& index = fixture.chain().headers();
    auto const genesis_pos = index.get_block_pos(index.find(genesis.hash()));
    auto const last_pos = index.get_block_pos(index.find(trunk.back().hash()));
    REQUIRE(genesis_pos.file == 0);
    REQUIRE(genesis_pos.pos == 8u);
    REQUIRE(last_pos.file == 0);
    auto const genesis_end = genesis_pos.pos + uint32_t(genesis.serialized_size());
    auto const records_end = last_pos.pos + uint32_t(trunk.back().serialized_size());
    auto const blocks_path = fixture.dir() / "blocks" / "blk00000.dat";
    fixture.close();

    // Take the genesis record out: the records after it move to the start of the
    // file, and the space they leave is zero, as reserved space is.
    {
        FILE* file = std::fopen(blocks_path.string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        data_chunk rest(records_end - genesis_end);
        REQUIRE(std::fseek(file, long(genesis_end), SEEK_SET) == 0);
        REQUIRE(std::fread(rest.data(), 1, rest.size(), file) == rest.size());
        REQUIRE(std::fseek(file, 0, SEEK_SET) == 0);
        REQUIRE(std::fwrite(rest.data(), 1, rest.size(), file) == rest.size());
        data_chunk const zeros(genesis_end, uint8_t{0});
        REQUIRE(std::fwrite(zeros.data(), 1, zeros.size(), file) == zeros.size());
        REQUIRE(std::fclose(file) == 0);
    }

    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const idx = chain.headers().find(genesis.hash());
    REQUIRE(idx != blockchain::header_index::null_index);
    CHECK(chain.headers().has_block_data(idx));
    // Written after the blocks that were there, not over them.
    auto const appended = chain.headers().get_block_pos(idx);
    CHECK(appended.file == 0);
    CHECK(appended.pos == records_end - genesis_end + 8u);

    auto const read = run_on_own_context(chain.fetch_block(size_t(0)));
    REQUIRE(read);
    CHECK(serialized(*read->first) == serialized(genesis));
    for (uint32_t h = 1; h <= 2; ++h) {
        CAPTURE(h);
        auto const block = run_on_own_context(chain.fetch_block(size_t(h)));
        REQUIRE(block);
        CHECK(serialized(*block->first) == serialized(trunk[h - 1]));
    }
    CHECK(chain.chain_view()->connected_tip_height == 2u);
}

TEST_CASE("a store that cannot write the genesis block does not start", "[block reads][genesis]") {
    test::chain_fixture fixture("block_reads_genesis_unwritable");
    REQUIRE(fixture.created());

    // The block files can be read, and so walked, but nothing can be written.
    auto const blocks = fixture.dir() / "blocks";
    std::filesystem::create_directories(blocks);
    std::filesystem::permissions(blocks,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
    // Given back however the test ends, so the fixture can remove the directory.
    struct writable_again {
        std::filesystem::path path;
        ~writable_again() {
            std::error_code ec;
            std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                std::filesystem::perm_options::replace, ec);
        }
    } const restore{blocks};

    auto const probe = blocks / "probe";
    if (FILE* file = std::fopen(probe.string().c_str(), "wb")) {
        std::fclose(file);
        std::filesystem::remove(probe);
        SKIP("this process can write to a directory it may only read (running as root, or a "
             "filesystem without permissions), so the failed write cannot be provoked here");
    }

    CHECK_FALSE(fixture.start());
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
