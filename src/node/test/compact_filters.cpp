// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/node/detail/compact_filter_store.hpp>
#include <kth/node/detail/compact_filters.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/parser.hpp>

using namespace kth;
using namespace kth::node::detail;
using kth::node::sync::execute_reorg;
using kth::node::sync::reorg_outcome;

namespace {

template <typename Awaitable>
auto run_on_own_context(Awaitable&& operation) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::forward<Awaitable>(operation), ::asio::use_future);
    io.run();
    return result.get();
}

using headers_answer = std::optional<domain::message::compact_filter_headers>;
using checkpoint_answer = std::optional<domain::message::compact_filter_checkpoint>;

// A regtest chain: `mined` blocks over genesis, the first `connected` of them
// connected and the rest held as headers only.
struct filtered_chain {
    // A record every four blocks, so a short chain holds some.
    static constexpr size_t interval = 4;

    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;
    uint32_t base_time;
    std::filesystem::path file;

    filtered_chain(char const* tag, uint32_t mined, uint32_t connected)
        : fixture(tag)
        , base_time(uint32_t(zulu_time()) - 60u * test::block_spacing)
        , file(std::filesystem::temp_directory_path()
            / ("kth_cf_index_" + std::string(tag) + "_" + std::to_string(getpid())))
    {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= mined; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
        connect(1, connected);
        std::filesystem::remove(file);
    }

    ~filtered_chain() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }

    // Connects the trunk blocks at heights `from` to `to`.
    void connect(uint32_t from, uint32_t to) {
        std::vector<domain::chain::block> const blocks(trunk.begin() + (from - 1), trunk.begin() + to);
        test::persist_headers(fixture, blocks, from);
        test::connect_bodies(fixture, blocks, from);
        REQUIRE(fixture.chain().chain_view()->connected_tip_height == to);
    }

    compact_filter_blocks blocks() {
        return compact_filter_blocks(fixture.chain(), domain::chain::block::genesis_regtest());
    }

    // The block at a height of the trunk.
    domain::chain::block block(size_t height) const {
        return height == 0 ? domain::chain::block::genesis_regtest() : trunk[height - 1];
    }

    hash_digest hash(size_t height) const {
        return block(height).hash();
    }
};

data_chunk filter_of(domain::chain::block const& block) {
    auto const built = domain::chain::build_regular_filter(block);
    REQUIRE(built);
    return built->to_data();
}

// The definition, spelled out over a list of blocks from the first one: all
// zeros before the first block.
std::vector<compact_filter_record> records_of(std::vector<domain::chain::block> const& blocks) {
    std::vector<compact_filter_record> records;
    hash_digest header = null_hash;
    for (auto const& block : blocks) {
        auto const filter_hash = bitcoin_hash(filter_of(block));
        header = bitcoin_hash(splice(filter_hash, header));
        records.push_back({filter_hash, header});
    }
    return records;
}

std::vector<compact_filter_record> records_of(filtered_chain const& c, size_t up_to) {
    std::vector<domain::chain::block> blocks;
    for (size_t h = 0; h <= up_to; ++h) {
        blocks.push_back(c.block(h));
    }
    return records_of(blocks);
}

std::vector<hash_digest> filter_hashes(std::vector<compact_filter_record> const& records, size_t from, size_t to) {
    std::vector<hash_digest> hashes;
    for (auto h = from; h <= to; ++h) {
        hashes.push_back(records[h].filter_hash);
    }
    return hashes;
}

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

compact_filter_index_pace pace_of(size_t slice = 32) {
    return {.slice = slice, .save_every = 1000, .interval = filtered_chain::interval};
}

// Runs the index over `c` with `scenario` beside it, on one thread, and ends
// the index when the scenario does. A failed assertion of the scenario is
// raised here, after both have ended.
template <typename Scenario>
void with_index(filtered_chain& c, compact_filter_index_pace pace, Scenario scenario) {
    compact_filter_channels channels;
    ::asio::io_context io;
    std::exception_ptr failure;
    ::asio::co_spawn(io, run_compact_filter_index(channels, c.blocks(), c.file, pace), ::asio::detached);
    ::asio::co_spawn(io, scenario(channels), [&](std::exception_ptr raised) {
        failure = raised;
        channels.close();
    });
    io.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

// What the index left in its file.
compact_filter_store stored(filtered_chain const& c) {
    compact_filter_store store(c.file);
    store.load();
    return store;
}

} // namespace

TEST_CASE("compact filter of a block is its regular filter, the first block included", "[compact filters]") {
    filtered_chain c("cf_filter", 13, 11);
    auto const blocks = c.blocks();
    for (size_t height = 0; height <= 11; ++height) {
        CAPTURE(height);
        auto const filter = run_on_own_context(build_compact_filter(blocks, c.hash(height)));
        REQUIRE(filter);
        CHECK(*filter == filter_of(c.block(height)));
    }
    // A block held as a header alone has no filter, and neither has one the
    // node never heard of.
    CHECK( ! run_on_own_context(build_compact_filter(blocks, c.hash(12))));
    CHECK( ! run_on_own_context(build_compact_filter(blocks, unknown_hash())));
}

TEST_CASE("compact filter range is of the connected chain and within its limit", "[compact filters]") {
    filtered_chain c("cf_range", 13, 11);
    auto const& chain = c.fixture.chain();

    auto const whole = compact_filter_range(chain, 0, c.hash(11), 12);
    REQUIRE(whole);
    REQUIRE(whole->size() == 12);
    for (size_t height = 0; height <= 11; ++height) {
        CAPTURE(height);
        CHECK((*whole)[height] == c.hash(height));
    }

    auto const inner = compact_filter_range(chain, 3, c.hash(5), 1000);
    REQUIRE(inner);
    CHECK(*inner == hash_list{c.hash(3), c.hash(4), c.hash(5)});

    auto const one = compact_filter_range(chain, 7, c.hash(7), 1);
    REQUIRE(one);
    CHECK(*one == hash_list{c.hash(7)});

    // Both ends count against the limit.
    CHECK( ! compact_filter_range(chain, 0, c.hash(11), 11));
    CHECK( ! compact_filter_range(chain, 3, c.hash(5), 2));
    // A start above the stop block.
    CHECK( ! compact_filter_range(chain, 6, c.hash(5), 1000));
    // A stop block above the connected tip, or one the node never heard of.
    CHECK( ! compact_filter_range(chain, 0, c.hash(12), 1000));
    CHECK( ! compact_filter_range(chain, 0, unknown_hash(), 1000));
}

TEST_CASE("compact filter index answers getcfheaders from its records", "[compact filters][index]") {
    filtered_chain c("cf_headers", 13, 11);
    auto const expected = records_of(c, 11);

    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // From the first block: the filter header before it is all zeros.
        auto const whole = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11)));
        REQUIRE(whole);
        CHECK(whole->filter_type == domain::message::regular_compact_filter);
        CHECK(whole->stop_hash == c.hash(11));
        CHECK(whole->previous_filter_header == null_hash);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 11));

        // From inside the chain: the filter header of the block before.
        auto const inner = co_await ask_compact_filter_headers(channels, headers_request(5, c.hash(9)));
        REQUIRE(inner);
        CHECK(inner->previous_filter_header == expected[4].filter_header);
        CHECK(inner->filter_hashes == filter_hashes(expected, 5, 9));

        auto const one = co_await ask_compact_filter_headers(channels, headers_request(11, c.hash(11)));
        REQUIRE(one);
        CHECK(one->previous_filter_header == expected[10].filter_header);
        CHECK(one->filter_hashes == filter_hashes(expected, 11, 11));
    });
}

TEST_CASE("compact filter index answers getcfcheckpt with every interval's filter header", "[compact filters][index]") {
    filtered_chain c("cf_checkpoint", 13, 11);
    auto const expected = records_of(c, 11);

    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const at_tip = co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(11)));
        REQUIRE(at_tip);
        CHECK(at_tip->filter_type == domain::message::regular_compact_filter);
        CHECK(at_tip->stop_hash == c.hash(11));
        CHECK(at_tip->filter_headers == std::vector{expected[4].filter_header, expected[8].filter_header});

        // Up to a block at an interval, that block included.
        auto const at_interval = co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(8)));
        REQUIRE(at_interval);
        CHECK(at_interval->filter_headers == std::vector{expected[4].filter_header, expected[8].filter_header});

        auto const below = co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(7)));
        REQUIRE(below);
        CHECK(below->filter_headers == std::vector{expected[4].filter_header});

        // Below the first interval there is none to carry, and that is an answer.
        auto const none = co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(3)));
        REQUIRE(none);
        CHECK(none->filter_headers.empty());
    });
}

TEST_CASE("compact filter index does not answer what is off the connected chain", "[compact filters][index]") {
    filtered_chain c("cf_refused", 13, 11);
    // A stored block on a side branch off height 2.
    auto const side = test::mine_block(c.hash(2), 3, c.base_time + 3 * test::block_spacing + 1, 7, {}, 0);
    (void)c.fixture.organizer().add_headers(test::headers_of(std::vector{side}));
    REQUIRE(run_on_own_context(c.fixture.chain().store_block(test::to_light(side), 3)) == error::success);

    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // The control: this one is answered.
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11))));

        // Another filter type.
        domain::message::get_compact_filter_headers other_type{1, 0, c.hash(11)};
        CHECK( ! co_await ask_compact_filter_headers(channels, other_type));
        domain::message::get_compact_filter_checkpoint other_checkpoint{1, c.hash(11)};
        CHECK( ! co_await ask_compact_filter_checkpoint(channels, other_checkpoint));

        // A stop block held as a header alone, on a side branch, or unknown.
        for (auto const& stop : {c.hash(12), side.hash(), unknown_hash()}) {
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, stop)));
            CHECK( ! co_await ask_compact_filter_checkpoint(channels, checkpoint_request(stop)));
        }

        // A start above the stop block.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(10, c.hash(9))));
    });
}

TEST_CASE("compact filter index answers between slices, up to where its records reach", "[compact filters][index]") {
    filtered_chain c("cf_slices", 13, 11);
    auto const expected = records_of(c, 11);

    // Four blocks a slice: the first look at the queries comes with the
    // records at height 3.
    with_index(c, pace_of(4), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const reached = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(3)));
        REQUIRE(reached);
        CHECK(reached->filter_hashes == filter_hashes(expected, 0, 3));
    });

    filtered_chain d("cf_slices_past", 13, 11);
    with_index(d, pace_of(4), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // Past the records: not answered now.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, d.hash(11))));
    });
}

TEST_CASE("compact filter index keeps its records in its file across runs", "[compact filters][index]") {
    filtered_chain c("cf_restart", 13, 11);
    auto const expected = records_of(c, 13);

    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11))));
    });
    {
        auto const store = stored(c);
        REQUIRE(store.size() == 12);
        CHECK(store.tip() == c.hash(11));
        for (size_t height = 0; height <= 11; ++height) {
            CAPTURE(height);
            CHECK(store.at(height) == expected[height]);
        }
    }

    // Two more blocks connect while the index is not running.
    c.connect(12, 13);
    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const whole = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(13)));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 13));
    });
    auto const store = stored(c);
    REQUIRE(store.size() == 14);
    CHECK(store.tip() == c.hash(13));
    CHECK(store.at(13) == expected[13]);
}

TEST_CASE("compact filter index follows the tip when it is rung", "[compact filters][index]") {
    filtered_chain c("cf_ring", 13, 11);
    auto const expected = records_of(c, 13);

    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11))));
        // Nothing past the connected tip.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(13))));

        c.connect(12, 13);
        // Connected and not rung: the index has not looked.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(13))));

        channels.tip_moved();
        auto const whole = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(13)));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 13));
        auto const checkpoint = co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(13)));
        REQUIRE(checkpoint);
        CHECK(checkpoint->filter_headers == std::vector{expected[4].filter_header,
            expected[8].filter_header, expected[12].filter_header});
    });
}

TEST_CASE("compact filter index drops the records of a branch the chain left", "[compact filters][index]") {
    filtered_chain c("cf_reorg", 8, 8);
    auto& chain = c.fixture.chain();

    // A heavier branch off height 5.
    std::vector<domain::chain::block> branch;
    auto prev = c.hash(5);
    for (uint32_t h = 6; h <= 10; ++h) {
        branch.push_back(test::mine_block(prev, h, c.base_time + h * test::block_spacing + 60, 2, {}, 0));
        prev = branch.back().hash();
    }
    std::vector<domain::chain::block> on_branch;
    for (size_t h = 0; h <= 5; ++h) {
        on_branch.push_back(c.block(h));
    }
    on_branch.insert(on_branch.end(), branch.begin(), branch.end());
    auto const expected = records_of(on_branch);
    auto const on_trunk = records_of(c, 8);

    auto const switch_to_branch = [&] {
        auto const added = c.fixture.organizer().add_headers(test::headers_of(branch));
        REQUIRE(added.reorg_candidate);
        reorg_outcome reorg;
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(chain, c.fixture.organizer(), added.reorg_branch_head, 5,
                [] { return false; }, test::real_persister(chain)),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
        REQUIRE(reorg.result.ok);
        REQUIRE(chain.chain_view()->connected_tip_height == 5u);
        test::connect_bodies(c.fixture, branch, 6);
        REQUIRE(chain.chain_view()->tip_hash == branch.back().hash());
    };

    SECTION("while it runs") {
        with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            auto const before = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(8)));
            REQUIRE(before);
            CHECK(before->filter_hashes == filter_hashes(on_trunk, 0, 8));

            switch_to_branch();
            // Asked before the ring: the trunk above the fork is not answered,
            // and neither is the branch, whose blocks have no record yet.
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(8))));
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, branch.back().hash())));
            // Nor is a branch block at a height the trunk had a record for:
            // that record is of another block.
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, branch.front().hash())));
            CHECK( ! co_await ask_compact_filter_checkpoint(channels, checkpoint_request(branch[2].hash())));
            // Below the fork it is the same chain.
            CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(5))));

            channels.tip_moved();
            auto const after = co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash()));
            REQUIRE(after);
            CHECK(after->filter_hashes == filter_hashes(expected, 0, 10));
            auto const checkpoint = co_await ask_compact_filter_checkpoint(channels,
                checkpoint_request(branch.back().hash()));
            REQUIRE(checkpoint);
            CHECK(checkpoint->filter_headers == std::vector{expected[4].filter_header, expected[8].filter_header});
            CHECK(expected[8].filter_header != on_trunk[8].filter_header);
        });
    }

    SECTION("while it does not") {
        with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(8))));
        });
        REQUIRE(stored(c).tip() == c.hash(8));

        switch_to_branch();
        // The file names a block the chain left.
        with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            auto const after = co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash()));
            REQUIRE(after);
            CHECK(after->filter_hashes == filter_hashes(expected, 0, 10));
        });
    }

    auto const store = stored(c);
    REQUIRE(store.size() == 11);
    CHECK(store.tip() == branch.back().hash());
    for (size_t height = 0; height <= 10; ++height) {
        CAPTURE(height);
        CHECK(store.at(height) == expected[height]);
    }
}

TEST_CASE("compact filter index computes again the records of a file it cannot place", "[compact filters][index]") {
    filtered_chain c("cf_misplaced", 13, 11);
    auto const expected = records_of(c, 11);

    // Records that chain, of blocks this node does not have.
    {
        compact_filter_store foreign(c.file);
        for (uint8_t i = 0; i < 20; ++i) {
            hash_digest hash{};
            hash[0] = i;
            foreign.append(hash);
        }
        REQUIRE( ! foreign.save(unknown_hash()));
    }
    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const whole = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11)));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 11));
    });
    REQUIRE(stored(c).size() == 12);

    // Records of this chain under the name of a block at another height.
    {
        compact_filter_store misnamed(c.file);
        misnamed.load();
        misnamed.truncate(9);
        REQUIRE( ! misnamed.save(c.hash(11)));
    }
    with_index(c, pace_of(), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const whole = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(11)));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 11));
    });
    auto const store = stored(c);
    REQUIRE(store.size() == 12);
    CHECK(store.tip() == c.hash(11));
}

TEST_CASE("compact filter query is refused when the index cannot take it", "[compact filters][index]") {
    filtered_chain c("cf_admission", 13, 11);

    // No index runs: the queries stay where they were put.
    compact_filter_channels channels;
    std::vector<std::shared_ptr<compact_filter_headers_query::reply_channel>> replies;
    for (size_t i = 0; i < compact_filter_channels::query_capacity; ++i) {
        replies.push_back(std::make_shared<compact_filter_headers_query::reply_channel>(1));
        REQUIRE(channels.queries.try_send(compact_filter_index_query{
            compact_filter_headers_query{headers_request(0, c.hash(11)), replies.back()}}));
    }
    // One past the capacity returns at once, with no answer.
    CHECK( ! run_on_own_context(ask_compact_filter_headers(channels, headers_request(0, c.hash(11)))));
    CHECK( ! run_on_own_context(ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(11)))));

    // After the close, too.
    compact_filter_channels closed;
    closed.close();
    CHECK( ! run_on_own_context(ask_compact_filter_headers(closed, headers_request(0, c.hash(11)))));
}

TEST_CASE("compact filter index answers what was queued when it is closed, and ends", "[compact filters][index]") {
    filtered_chain c("cf_close", 13, 11);
    auto const expected = records_of(c, 11);

    compact_filter_channels channels;
    auto const reply = std::make_shared<compact_filter_headers_query::reply_channel>(1);
    REQUIRE(channels.queries.try_send(compact_filter_index_query{
        compact_filter_headers_query{headers_request(0, c.hash(11)), reply}}));
    channels.close();

    // Closed before it starts: it reads the chain once, answers and ends.
    run_on_own_context(run_compact_filter_index(channels, c.blocks(), c.file, pace_of()));
    std::error_code ec;
    auto const answered = reply->try_receive(ec);
    REQUIRE(answered);
    REQUIRE(*answered);
    CHECK((*answered)->filter_hashes == filter_hashes(expected, 0, 11));
    CHECK(stored(c).size() == 12);
}

TEST_CASE("compact filters are off unless asked for, and refused on a pruning node", "[compact filters][parser]") {
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest"};
        std::ostringstream error;
        REQUIRE(parser.parse(3, argv, error));
        CHECK( ! parser.configured.node.compact_filters);
    }
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.compact_filters", "true"};
        std::ostringstream error;
        REQUIRE(parser.parse(5, argv, error));
        CHECK(parser.configured.node.compact_filters);
    }
    {
        // A pruning node alone is fine.
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--db.prune_keep_blocks", "288"};
        std::ostringstream error;
        REQUIRE(parser.parse(5, argv, error));
    }
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.compact_filters", "true",
            "--db.prune_keep_blocks", "288"};
        std::ostringstream error;
        CHECK( ! parser.parse(7, argv, error));
        CHECK(error.str().find("node.compact_filters is not allowed on a pruning node") != std::string::npos);
    }
}

TEST_CASE("a node announces compact filters when it serves them, and never while it prunes", "[compact filters][node]") {
    auto const compact_filters = uint64_t(domain::message::version::service::node_compact_filters);
    auto const dir = std::filesystem::temp_directory_path() / ("kth_cf_node_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    auto const config = [&dir](bool filters, uint32_t prune_keep_blocks) {
        node::configuration cfg{domain::config::network::regtest};
        cfg.database.directory = dir;
        cfg.database.db_max_size = 16ULL << 20;
        cfg.network.threads = 0;
        cfg.network.peers_file = dir / "peers.dat";
        cfg.network.hosts_file = dir / "hosts.dat";
        cfg.node.compact_filters = filters;
        cfg.database.prune_keep_blocks = prune_keep_blocks;
        return cfg;
    };

    {
        // Off: the service bit is not announced.
        node::full_node node{config(false, 0)};
        CHECK((node.network_settings().services & compact_filters) == 0);
    }
    {
        node::full_node node{config(true, 0)};
        CHECK((node.network_settings().services & compact_filters) != 0);
    }
    {
        // On a pruning node it is not announced.
        node::full_node node{config(true, 288)};
        CHECK((node.network_settings().services & compact_filters) == 0);
    }
    std::filesystem::remove_all(dir, ec);
}
