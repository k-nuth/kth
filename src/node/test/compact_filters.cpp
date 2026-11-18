// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/node/detail/compact_filters.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using node::detail::compact_filter_checkpoint_record;
using node::detail::compact_filter_service;

namespace {

template <typename Awaitable>
auto run_on_own_context(Awaitable&& operation) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::forward<Awaitable>(operation), ::asio::use_future);
    io.run();
    return result.get();
}

// Eleven connected blocks over genesis, two more as headers only, and a
// stored one-block side branch off height 2. Records every four blocks, so
// the chain holds two of them, at heights 4 and 8.
struct filtered_chain {
    static constexpr size_t interval = 4;

    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;
    domain::chain::block side;
    std::filesystem::path file;

    explicit filtered_chain(char const* tag) : fixture(tag) {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto& chain = fixture.chain();
        auto const base_time = uint32_t(zulu_time()) - 60u * test::block_spacing;
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= 13; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        side = test::mine_block(trunk[1].hash(), 3, base_time + 3 * test::block_spacing + 1, 7, {}, 0);

        REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
        (void)fixture.organizer().add_headers(test::headers_of(std::vector{side}));
        REQUIRE(run_on_own_context(chain.store_block(test::to_light(side), 3)) == error::success);
        std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 11);
        test::persist_headers(fixture, connected, 1);
        test::connect_bodies(fixture, connected, 1);
        REQUIRE(chain.chain_view()->connected_tip_height == 11u);

        file = std::filesystem::temp_directory_path() / (std::string("kth_") + tag + "_filter_checkpoints");
        std::filesystem::remove(file);
    }

    ~filtered_chain() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }

    compact_filter_service service() {
        return compact_filter_service(fixture.chain(), domain::chain::block::genesis_regtest(), file, interval);
    }

    // The block at a height of the connected chain.
    domain::chain::block block(size_t height) const {
        return height == 0 ? domain::chain::block::genesis_regtest() : trunk[height - 1];
    }

    data_chunk filter(size_t height) const {
        auto const built = domain::chain::build_regular_filter(block(height));
        REQUIRE(built);
        return built->to_data();
    }

    hash_digest filter_hash(size_t height) const {
        return bitcoin_hash(filter(height));
    }

    // The definition, spelled out: all zeros before the first block.
    hash_digest filter_header(size_t height) const {
        hash_digest header = null_hash;
        for (size_t h = 0; h <= height; ++h) {
            header = bitcoin_hash(splice(filter_hash(h), header));
        }
        return header;
    }
};

domain::message::get_compact_filter_headers headers_request(uint32_t start, hash_digest const& stop) {
    return {domain::message::regular_compact_filter, start, stop};
}

domain::message::get_compact_filter_checkpoint checkpoint_request(hash_digest const& stop) {
    return {domain::message::regular_compact_filter, stop};
}

hash_digest unknown_hash() {
    hash_digest hash{};
    hash[0] = 0x42;
    return hash;
}

} // namespace

TEST_CASE("compact filters are the regular filter of each block, built once", "[compact filters]") {
    filtered_chain c("cf_filters");
    auto service = c.service();
    for (size_t height = 0; height <= 11; ++height) {
        CAPTURE(height);
        auto const filter = run_on_own_context(service.filter_of(c.block(height).hash()));
        REQUIRE(filter);
        REQUIRE(*filter);
        CHECK(**filter == c.filter(height));
        // The second answer is the same object: the block is not read again.
        auto const again = run_on_own_context(service.filter_of(c.block(height).hash()));
        REQUIRE(again);
        CHECK(again->get() == filter->get());
    }
    // A stored block off the connected chain has a filter too: the range
    // rules decide what a peer is sent, not the builder.
    auto const side = run_on_own_context(service.filter_of(c.side.hash()));
    REQUIRE(side);
    // A block the node holds as a header alone has none.
    CHECK( ! run_on_own_context(service.filter_of(c.trunk[12].hash())));
    CHECK( ! run_on_own_context(service.filter_of(unknown_hash())));
}

TEST_CASE("compact filters past the memory budget are built again", "[compact filters]") {
    filtered_chain c("cf_budget");
    // Room for either of two filters, and not for both.
    auto const budget = std::max(c.filter(1).size(), c.filter(2).size());
    compact_filter_service service(c.fixture.chain(), domain::chain::block::genesis_regtest(), c.file,
        filtered_chain::interval, budget, 1);
    auto const first = run_on_own_context(service.filter_of(c.block(1).hash()));
    REQUIRE(first);
    auto const second = run_on_own_context(service.filter_of(c.block(2).hash()));
    REQUIRE(second);
    auto const first_again = run_on_own_context(service.filter_of(c.block(1).hash()));
    REQUIRE(first_again);
    CHECK(**first_again == c.filter(1));
    CHECK(first_again->get() != first->get());
}

TEST_CASE("compact filter ranges are of the connected chain, up to a limit", "[compact filters]") {
    filtered_chain c("cf_ranges");
    auto service = c.service();
    auto const hashes = [&](size_t first, size_t last) {
        hash_list out;
        for (auto h = first; h <= last; ++h) out.push_back(c.block(h).hash());
        return out;
    };
    // The first block is one of them.
    CHECK(service.blocks_of(0, c.block(3).hash(), 1000) == hashes(0, 3));
    CHECK(service.blocks_of(5, c.block(11).hash(), 1000) == hashes(5, 11));
    CHECK(service.blocks_of(7, c.block(7).hash(), 1000) == hashes(7, 7));
    // The limit counts both ends.
    CHECK(service.blocks_of(2, c.block(6).hash(), 5) == hashes(2, 6));
    CHECK( ! service.blocks_of(2, c.block(6).hash(), 4));
    // A start above the stop block.
    CHECK( ! service.blocks_of(8, c.block(7).hash(), 1000));
    CHECK( ! service.blocks_of(0xffffffff, c.block(7).hash(), 1000));
    // Stop blocks that are not on the connected chain: a header above the
    // tip, the side branch, and one the node never heard of.
    CHECK( ! service.blocks_of(0, c.trunk[11].hash(), 1000));
    CHECK( ! service.blocks_of(0, c.side.hash(), 1000));
    CHECK( ! service.blocks_of(0, unknown_hash(), 1000));
}

TEST_CASE("cfheaders chains from the first block until a record exists below the range", "[compact filters]") {
    filtered_chain c("cf_headers");
    auto service = c.service();

    // From the first block: the header before it is all zeros.
    auto const first = run_on_own_context(service.headers(headers_request(0, c.block(3).hash())));
    REQUIRE(first);
    CHECK(first->filter_type == domain::message::regular_compact_filter);
    CHECK(first->stop_hash == c.block(3).hash());
    CHECK(first->previous_filter_header == null_hash);
    REQUIRE(first->filter_hashes.size() == 4);
    for (size_t h = 0; h <= 3; ++h) CHECK(first->filter_hashes[h] == c.filter_hash(h));

    // Below the first record everything is chained from the first block.
    auto const low = run_on_own_context(service.headers(headers_request(3, c.block(4).hash())));
    REQUIRE(low);
    CHECK(low->previous_filter_header == c.filter_header(2));
    CHECK(low->filter_hashes == std::vector<hash_digest>{c.filter_hash(3), c.filter_hash(4)});

    // A range that starts above a record needs that record: it is not there yet.
    CHECK( ! run_on_own_context(service.headers(headers_request(6, c.block(9).hash()))));

    run_on_own_context(service.extend());
    auto const high = run_on_own_context(service.headers(headers_request(6, c.block(9).hash())));
    REQUIRE(high);
    CHECK(high->previous_filter_header == c.filter_header(5));
    REQUIRE(high->filter_hashes.size() == 4);
    for (size_t h = 6; h <= 9; ++h) CHECK(high->filter_hashes[h - 6] == c.filter_hash(h));
    // The block right after a record: the record itself is the header before.
    auto const edge = run_on_own_context(service.headers(headers_request(9, c.block(11).hash())));
    REQUIRE(edge);
    CHECK(edge->previous_filter_header == c.filter_header(8));
    // And the record's own block: the header before is one block below it.
    auto const at = run_on_own_context(service.headers(headers_request(8, c.block(8).hash())));
    REQUIRE(at);
    CHECK(at->previous_filter_header == c.filter_header(7));
    CHECK(at->filter_hashes == std::vector<hash_digest>{c.filter_hash(8)});

    // What is not answered.
    auto other = headers_request(0, c.block(3).hash());
    other.filter_type = 1;
    CHECK( ! run_on_own_context(service.headers(other)));
    CHECK( ! run_on_own_context(service.headers(headers_request(4, c.block(3).hash()))));
    CHECK( ! run_on_own_context(service.headers(headers_request(0, c.side.hash()))));
    CHECK( ! run_on_own_context(service.headers(headers_request(0, c.trunk[12].hash()))));
    CHECK( ! run_on_own_context(service.headers(headers_request(0, unknown_hash()))));
}

TEST_CASE("filter header records follow the connected chain and answer cfcheckpt", "[compact filters]") {
    filtered_chain c("cf_checkpoint");
    auto service = c.service();
    service.load();
    CHECK(service.checkpoints().empty());

    // Below the first record there is nothing to carry, computed or not.
    auto const none = service.checkpoint(checkpoint_request(c.block(3).hash()));
    REQUIRE(none);
    CHECK(none->filter_headers.empty());
    CHECK(none->stop_hash == c.block(3).hash());
    // Above it, the records are not there yet.
    CHECK( ! service.checkpoint(checkpoint_request(c.block(11).hash())));

    run_on_own_context(service.extend());
    std::vector<compact_filter_checkpoint_record> const expected{
        {c.block(4).hash(), c.filter_header(4)}, {c.block(8).hash(), c.filter_header(8)}};
    CHECK(service.checkpoints() == expected);

    auto const all = service.checkpoint(checkpoint_request(c.block(11).hash()));
    REQUIRE(all);
    CHECK(all->filter_type == domain::message::regular_compact_filter);
    CHECK(all->stop_hash == c.block(11).hash());
    CHECK(all->filter_headers == std::vector<hash_digest>{c.filter_header(4), c.filter_header(8)});
    // Up to a stop block: the records at or below it.
    auto const one = service.checkpoint(checkpoint_request(c.block(7).hash()));
    REQUIRE(one);
    CHECK(one->filter_headers == std::vector<hash_digest>{c.filter_header(4)});
    auto const exact = service.checkpoint(checkpoint_request(c.block(8).hash()));
    REQUIRE(exact);
    CHECK(exact->filter_headers.size() == 2);

    auto other = checkpoint_request(c.block(11).hash());
    other.filter_type = 1;
    CHECK( ! service.checkpoint(other));
    CHECK( ! service.checkpoint(checkpoint_request(c.side.hash())));
    CHECK( ! service.checkpoint(checkpoint_request(c.trunk[12].hash())));
    CHECK( ! service.checkpoint(checkpoint_request(unknown_hash())));

    // A second pass with nothing new changes nothing.
    run_on_own_context(service.extend());
    CHECK(service.checkpoints() == expected);
    // A pass told to stop does nothing.
    auto stopped = c.service();
    run_on_own_context(stopped.extend([] { return true; }));
    CHECK(stopped.checkpoints().empty());
}

TEST_CASE("filter header records are kept in one small file", "[compact filters]") {
    filtered_chain c("cf_file");
    {
        auto service = c.service();
        run_on_own_context(service.extend());
        REQUIRE(service.checkpoints().size() == 2);
    }
    // 64 bytes a record, and nothing else.
    REQUIRE(std::filesystem::file_size(c.file) == 2u * 64u);

    // Another run of the node reads them and answers without a pass.
    auto reopened = c.service();
    reopened.load();
    REQUIRE(reopened.checkpoints().size() == 2);
    auto const answer = reopened.checkpoint(checkpoint_request(c.block(11).hash()));
    REQUIRE(answer);
    CHECK(answer->filter_headers == std::vector<hash_digest>{c.filter_header(4), c.filter_header(8)});

    // Records of another chain are dropped and computed again.
    {
        std::ofstream file(c.file, std::ios::binary | std::ios::trunc);
        data_chunk const foreign(3 * 64, 0x5a);
        file.write(reinterpret_cast<char const*>(foreign.data()), std::streamsize(foreign.size()));
    }
    auto replaced = c.service();
    replaced.load();
    REQUIRE(replaced.checkpoints().size() == 3);
    CHECK( ! replaced.checkpoint(checkpoint_request(c.block(11).hash())));
    run_on_own_context(replaced.extend());
    REQUIRE(replaced.checkpoints().size() == 2);
    CHECK(replaced.checkpoints()[1].filter_header == c.filter_header(8));
    REQUIRE(std::filesystem::file_size(c.file) == 2u * 64u);

    // A file that is not whole records is ignored.
    {
        std::ofstream file(c.file, std::ios::binary | std::ios::trunc);
        data_chunk const torn(64 + 7, 0x11);
        file.write(reinterpret_cast<char const*>(torn.data()), std::streamsize(torn.size()));
    }
    auto torn = c.service();
    torn.load();
    CHECK(torn.checkpoints().empty());
    // No file at all is an empty list.
    std::filesystem::remove(c.file);
    auto fresh = c.service();
    fresh.load();
    CHECK(fresh.checkpoints().empty());
}
