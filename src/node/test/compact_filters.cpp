// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/detail/compact_filter_store.hpp>
#include <kth/node/detail/compact_filters.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/parser.hpp>

using namespace kth;
using namespace kth::node::detail;
using kth::node::sync::execute_reorg;
using kth::node::sync::reorg_outcome;

namespace {

int process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return ::getpid();
#endif
}

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
// connected and the rest held as headers only. From `first_spend` on (0 for
// none), each block also spends the coinbase of the block 100 below it, which
// matures there, so its input prefix filter holds a value: below, a block holds
// its coinbase alone and its input prefix filter is empty.
struct filtered_chain {
    // A record every four blocks, so a short chain holds some.
    static constexpr size_t interval = 4;

    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;
    uint32_t base_time;
    uint32_t spend_from;
    std::filesystem::path file;
    // Where an index that keeps the input prefix filter keeps its records.
    std::filesystem::path input_prefix_file;

    filtered_chain(char const* tag, uint32_t mined, uint32_t connected, uint32_t first_spend = 0)
        : fixture(tag)
        , base_time(uint32_t(zulu_time()) - (60u + mined) * test::block_spacing)
        , spend_from(first_spend)
        , file(std::filesystem::temp_directory_path()
            / ("kth_cf_index_" + std::string(tag) + "_" + std::to_string(process_id())))
        , input_prefix_file(std::filesystem::temp_directory_path()
            / ("kth_ipf_index_" + std::string(tag) + "_" + std::to_string(process_id())))
    {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= mined; ++h) {
            trunk.push_back(mine(prev, h, 0, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
        connect(1, connected);
        std::filesystem::remove(file);
        std::filesystem::remove(input_prefix_file);
    }

    ~filtered_chain() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(input_prefix_file, ignored);
    }

    // A block at `height` over `prev`, `seconds` after the trunk's time for
    // that height. Another `salt` gives another block: another coinbase and,
    // from spend_from on, a spend with another fee, so another input.
    domain::chain::block mine(hash_digest const& prev, uint32_t height, uint32_t salt, uint32_t seconds) const {
        std::vector<domain::chain::transaction> spends;
        uint64_t fee = 0;
        if (spend_from != 0 && height >= spend_from) {
            auto const& matured = trunk[height - coinbase_maturity - 1].transactions().front();
            fee = 1000 + salt;
            spends.push_back(test::spend_p2pkh(matured, 0, matured.outputs()[0].value() - fee,
                blockchain::settings(domain::config::network::regtest).enabled_flags()));
        }
        return test::mine_block(prev, height, base_time + height * test::block_spacing + seconds, salt,
            std::move(spends), fee);
    }

    // Connects the trunk blocks at heights `from` to `to`.
    void connect(uint32_t from, uint32_t to) {
        std::vector<domain::chain::block> const blocks(trunk.begin() + (from - 1), trunk.begin() + to);
        test::persist_headers(fixture, blocks, from);
        test::connect_bodies(fixture, blocks, from);
        REQUIRE(fixture.chain().chain_view()->connected_tip_height == to);
    }

    compact_filter_blocks blocks(bool input_prefix = false) {
        return compact_filter_blocks(fixture.chain(), domain::chain::block::genesis_regtest(), input_prefix);
    }

    // Both filter types, each in its file.
    std::vector<compact_filter_index_file> both_files() const {
        return {{domain::message::regular_compact_filter, file},
                {domain::message::input_prefix_compact_filter, input_prefix_file}};
    }

    // The block at a height of the trunk.
    domain::chain::block block(size_t height) const {
        return height == 0 ? domain::chain::block::genesis_regtest() : trunk[height - 1];
    }

    hash_digest hash(size_t height) const {
        return block(height).hash();
    }
};

constexpr auto regular_type = domain::message::regular_compact_filter;
constexpr auto input_prefix_type = domain::message::input_prefix_compact_filter;

data_chunk filter_of(domain::chain::block const& block, uint8_t filter_type = regular_type) {
    if (filter_type == input_prefix_type) {
        return domain::chain::to_data(domain::chain::build_input_prefix_filter(block));
    }
    auto const built = domain::chain::build_regular_filter(block);
    REQUIRE(built);
    return built->to_data();
}

// The definition, spelled out over a list of blocks from the first one: all
// zeros before the first block.
std::vector<compact_filter_record> records_of(std::vector<domain::chain::block> const& blocks,
    uint8_t filter_type = regular_type) {
    std::vector<compact_filter_record> records;
    hash_digest header = null_hash;
    for (auto const& block : blocks) {
        auto const filter_hash = bitcoin_hash(filter_of(block, filter_type));
        header = bitcoin_hash(splice(filter_hash, header));
        records.push_back({filter_hash, header});
    }
    return records;
}

std::vector<compact_filter_record> records_of(filtered_chain const& c, size_t up_to, uint8_t filter_type = regular_type) {
    std::vector<domain::chain::block> blocks;
    for (size_t h = 0; h <= up_to; ++h) {
        blocks.push_back(c.block(h));
    }
    return records_of(blocks, filter_type);
}

std::vector<hash_digest> filter_hashes(std::vector<compact_filter_record> const& records, size_t from, size_t to) {
    std::vector<hash_digest> hashes;
    for (auto h = from; h <= to; ++h) {
        hashes.push_back(records[h].filter_hash);
    }
    return hashes;
}

domain::message::get_compact_filter_headers headers_request(uint32_t start, hash_digest const& stop,
    uint8_t filter_type = regular_type) {
    return {filter_type, start, stop};
}

domain::message::get_compact_filter_checkpoint checkpoint_request(hash_digest const& stop,
    uint8_t filter_type = regular_type) {
    return {filter_type, stop};
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

// The same, for an index that keeps the records of each of `files`, and serves
// the input prefix filter when one of them is of it.
template <typename Scenario>
void with_index_of(filtered_chain& c, std::vector<compact_filter_index_file> files,
    compact_filter_index_pace pace, Scenario scenario) {
    auto const input_prefix = std::ranges::any_of(files, [](auto const& kept) {
        return kept.filter_type == input_prefix_type;
    });
    compact_filter_channels channels;
    ::asio::io_context io;
    std::exception_ptr failure;
    ::asio::co_spawn(io, run_compact_filter_index(channels, c.blocks(input_prefix), std::move(files), pace),
        ::asio::detached);
    ::asio::co_spawn(io, scenario(channels), [&](std::exception_ptr raised) {
        failure = raised;
        channels.close();
    });
    io.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

// What the index left in a file.
compact_filter_store stored_in(std::filesystem::path const& file) {
    compact_filter_store store(file);
    store.load();
    return store;
}

compact_filter_store stored(filtered_chain const& c) {
    return stored_in(c.file);
}

data_chunk bytes_of(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    return data_chunk(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The value of an input, by the definition: the first two bytes, read
// big-endian, of the double SHA-256 of the input as it stands in its
// transaction: the outpoint, the script with its length and the sequence.
uint16_t value_of(domain::chain::input const& in) {
    data_chunk bytes;
    auto const& point = in.previous_output();
    extend_data(bytes, point.hash());
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(uint8_t(point.index() >> (8 * i)));
    }
    auto const script = to_data_chunk(in.script(), false);
    REQUIRE(script.size() < 0xfd);
    bytes.push_back(uint8_t(script.size()));
    extend_data(bytes, script);
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(uint8_t(in.sequence() >> (8 * i)));
    }
    auto const digest = bitcoin_hash(bytes);
    return uint16_t((digest[0] << 8) | digest[1]);
}

// The filter of one value, by the definition: the count 1, the Rice parameter
// 15, then the value's 15th bit and up in one-bits, a zero-bit and its low 15
// bits, most significant first, padded with zero-bits.
data_chunk one_value_filter(uint16_t value) {
    if (value < 0x8000) {
        return {0x01, 0x0f, uint8_t(value >> 8), uint8_t(value)};
    }
    auto const bits = ((uint32_t(0b10) << 15) | (value & 0x7fffu)) << 7;
    return {0x01, 0x0f, uint8_t(bits >> 16), uint8_t(bits >> 8), uint8_t(bits)};
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

    // Records that chain, of other blocks, under the name of a block this
    // node has at another height: nothing but the height tells them apart
    // from this chain's.
    {
        compact_filter_store misnamed(c.file);
        for (uint8_t i = 0; i < 9; ++i) {
            hash_digest hash{};
            hash[0] = i;
            misnamed.append(hash);
        }
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
    // One answer, and the reply is closed behind it.
    CHECK( ! reply->try_receive(ec));
    CHECK(ec == ::asio::error::eof);
    CHECK(stored(c).size() == 12);
}

TEST_CASE("compact filter index closes the reply of a query it ends without answering", "[compact filters][index]") {
    filtered_chain c("cf_unanswered", 13, 11);

    compact_filter_channels channels;
    ::asio::io_context io;
    ::asio::cancellation_signal cancel;
    bool ended = false;
    std::exception_ptr raised;
    ::asio::co_spawn(io, run_compact_filter_index(channels, c.blocks(), c.file, pace_of()),
        ::asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr exception) {
            ended = true;
            raised = exception;
        }));
    // Up to where it waits for a ring or a query.
    while (io.poll() > 0) {}
    REQUIRE( ! ended);

    // Cancelled, then rung: it wakes to look at the chain, and its next wait
    // throws. The query put in meanwhile is one it never sees.
    cancel.emit(::asio::cancellation_type::terminal);
    channels.tip_moved();
    auto const reply = std::make_shared<compact_filter_headers_query::reply_channel>(1);
    REQUIRE(channels.queries.try_send(compact_filter_index_query{
        compact_filter_headers_query{headers_request(0, c.hash(11)), reply}}));
    io.run();
    REQUIRE(ended);
    REQUIRE(raised);

    std::error_code ec;
    CHECK( ! reply->try_receive(ec));
    CHECK(ec == ::asio::error::eof);
    // And nothing more is taken.
    CHECK( ! channels.queries.try_send(compact_filter_index_query{
        compact_filter_headers_query{headers_request(0, c.hash(11)), reply}}));
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
    auto const dir = std::filesystem::temp_directory_path() / ("kth_cf_node_" + std::to_string(process_id()));
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

// =============================================================================
// The input prefix filter (filter type 2)
// =============================================================================
//
// Served beside the regular filter by the same index, its records in a file of
// their own. The chains below spend a matured coinbase in each block from
// height 101 on, so the input prefix filter of such a block holds the value of
// that spend's input; below, a block holds its coinbase alone and its filter
// holds no value.

TEST_CASE("input prefix filter of a block is built from the block, the first block included",
    "[compact filters][input prefix]") {
    filtered_chain c("ipf_filter", 104, 103, 101);
    auto const blocks = c.blocks(true);
    CHECK(blocks.serves(regular_type));
    CHECK(blocks.serves(input_prefix_type));
    CHECK( ! blocks.serves(1));
    CHECK( ! blocks.serves(3));
    CHECK( ! c.blocks().serves(input_prefix_type));

    for (size_t height = 0; height <= 103; ++height) {
        CAPTURE(height);
        auto const filter = run_on_own_context(build_compact_filter(blocks, input_prefix_type, c.hash(height)));
        REQUIRE(filter);
        CHECK(*filter == filter_of(c.block(height), input_prefix_type));
        if (height < 101) {
            // A coinbase alone: the count 0, and nothing after it.
            CHECK(*filter == data_chunk{0x00});
            continue;
        }
        // One spend: the value of its input, the one value of the block, Rice
        // coded with the parameter of a single value.
        auto const& transactions = c.block(height).transactions();
        REQUIRE(transactions.size() == 2);
        auto const value = value_of(transactions[1].inputs().front());
        CHECK(*filter == one_value_filter(value));
        auto const parsed = domain::chain::parse_input_prefix_filter(*filter);
        REQUIRE(parsed);
        CHECK(parsed->values == std::vector<uint16_t>{value});
    }

    // The regular filter, from the same blocks.
    auto const regular = run_on_own_context(build_compact_filter(blocks, regular_type, c.hash(102)));
    REQUIRE(regular);
    CHECK(*regular == filter_of(c.block(102)));

    // A block held as a header alone, a block the node never heard of, and
    // filter types that are not built.
    CHECK( ! run_on_own_context(build_compact_filter(blocks, input_prefix_type, c.hash(104))));
    CHECK( ! run_on_own_context(build_compact_filter(blocks, input_prefix_type, unknown_hash())));
    CHECK( ! run_on_own_context(build_compact_filter(blocks, 1, c.hash(103))));
    CHECK( ! run_on_own_context(build_compact_filter(blocks, 3, c.hash(103))));
}

TEST_CASE("input prefix filter index answers getcfheaders and getcfcheckpt from its records",
    "[compact filters][input prefix][index]") {
    filtered_chain c("ipf_headers", 110, 108, 101);
    auto const expected = records_of(c, 108, input_prefix_type);
    auto const regular = records_of(c, 108);

    with_index_of(c, c.both_files(), pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // From the first block: the filter header before it is all zeros.
        auto const whole = co_await ask_compact_filter_headers(channels,
            headers_request(0, c.hash(108), input_prefix_type));
        REQUIRE(whole);
        CHECK(whole->filter_type == input_prefix_type);
        CHECK(whole->stop_hash == c.hash(108));
        CHECK(whole->previous_filter_header == null_hash);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 108));

        // From inside the chain, across the first block with a value: the
        // filter header of the block before.
        auto const inner = co_await ask_compact_filter_headers(channels,
            headers_request(99, c.hash(104), input_prefix_type));
        REQUIRE(inner);
        CHECK(inner->previous_filter_header == expected[98].filter_header);
        CHECK(inner->filter_hashes == filter_hashes(expected, 99, 104));

        auto const one = co_await ask_compact_filter_headers(channels,
            headers_request(108, c.hash(108), input_prefix_type));
        REQUIRE(one);
        CHECK(one->previous_filter_header == expected[107].filter_header);
        CHECK(one->filter_hashes == filter_hashes(expected, 108, 108));

        // A wallet chains the filter hashes from zeros, and the checkpoints
        // are every fourth block's header of that chain.
        std::vector<hash_digest> chained;
        hash_digest header = null_hash;
        for (auto const& filter_hash : whole->filter_hashes) {
            header = bitcoin_hash(splice(filter_hash, header));
            chained.push_back(header);
        }
        auto const at_tip = co_await ask_compact_filter_checkpoint(channels,
            checkpoint_request(c.hash(108), input_prefix_type));
        REQUIRE(at_tip);
        CHECK(at_tip->filter_type == input_prefix_type);
        CHECK(at_tip->stop_hash == c.hash(108));
        REQUIRE(at_tip->filter_headers.size() == 27);
        for (size_t i = 0; i < at_tip->filter_headers.size(); ++i) {
            CAPTURE(i);
            CHECK(at_tip->filter_headers[i] == chained[(i + 1) * filtered_chain::interval]);
        }
        auto const below = co_await ask_compact_filter_checkpoint(channels,
            checkpoint_request(c.hash(107), input_prefix_type));
        REQUIRE(below);
        CHECK(below->filter_headers.size() == 26);
        auto const none = co_await ask_compact_filter_checkpoint(channels,
            checkpoint_request(c.hash(3), input_prefix_type));
        REQUIRE(none);
        CHECK(none->filter_headers.empty());

        // The regular filter is answered beside it, from its own records.
        auto const regular_headers = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(108)));
        REQUIRE(regular_headers);
        CHECK(regular_headers->filter_hashes == filter_hashes(regular, 0, 108));
        auto const regular_checkpoint = co_await ask_compact_filter_checkpoint(channels,
            checkpoint_request(c.hash(108)));
        REQUIRE(regular_checkpoint);
        REQUIRE(regular_checkpoint->filter_headers.size() == 27);
        CHECK(regular_checkpoint->filter_headers.back() == regular[108].filter_header);
        CHECK(regular[108].filter_header != expected[108].filter_header);
    });

    // Each type's records in its own file.
    auto const kept = stored_in(c.input_prefix_file);
    REQUIRE(kept.size() == 109);
    CHECK(kept.tip() == c.hash(108));
    for (size_t height = 0; height <= 108; ++height) {
        CAPTURE(height);
        CHECK(kept.at(height) == expected[height]);
    }
    auto const regular_kept = stored(c);
    REQUIRE(regular_kept.size() == 109);
    CHECK(regular_kept.at(108) == regular[108]);
}

TEST_CASE("input prefix filter index does not answer another type, a block off the connected chain or a bad range",
    "[compact filters][input prefix][index]") {
    filtered_chain c("ipf_refused", 104, 103, 101);
    // A stored block on a side branch off height 101.
    auto const side = c.mine(c.hash(101), 102, 7, 1);
    (void)c.fixture.organizer().add_headers(test::headers_of(std::vector{side}));
    REQUIRE(run_on_own_context(c.fixture.chain().store_block(test::to_light(side), 102)) == error::success);

    with_index_of(c, c.both_files(), pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // The control: these are answered.
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(103), input_prefix_type)));
        CHECK(co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(103), input_prefix_type)));

        // Filter types the index keeps no records of: the script filter, the
        // one after the input prefix filter, and the last there is.
        for (auto const type : {uint8_t(1), uint8_t(3), uint8_t(255)}) {
            CAPTURE(int(type));
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(103), type)));
            CHECK( ! co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(103), type)));
        }

        // A stop block held as a header alone, on a side branch, or unknown.
        for (auto const& stop : {c.hash(104), side.hash(), unknown_hash()}) {
            CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, stop, input_prefix_type)));
            CHECK( ! co_await ask_compact_filter_checkpoint(channels, checkpoint_request(stop, input_prefix_type)));
        }

        // A start above the stop block.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(103, c.hash(102), input_prefix_type)));
    });

    // A node that serves the regular filter alone answers nothing about the
    // input prefix filter, which its index keeps no records of.
    CHECK( ! c.blocks().serves(input_prefix_type));
    with_index(c, pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(103))));
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(103), input_prefix_type)));
        CHECK( ! co_await ask_compact_filter_checkpoint(channels, checkpoint_request(c.hash(103), input_prefix_type)));
    });
}

TEST_CASE("input prefix filter requests past their limits are not answered", "[compact filters][input prefix][index]") {
    // 2,001 blocks over genesis: 2,002 filter headers.
    filtered_chain c("ipf_limits", 2001, 2001);
    auto const& chain = c.fixture.chain();

    // getcfilters: at most 1,000 filters, both ends counted.
    CHECK( ! compact_filter_range(chain, 0, c.hash(1000), domain::message::max_compact_filters_per_request));
    auto const thousand = compact_filter_range(chain, 1, c.hash(1000), domain::message::max_compact_filters_per_request);
    REQUIRE(thousand);
    CHECK(thousand->size() == 1000);

    // The whole chain in one slice, so every record is there when asked.
    with_index_of(c, c.both_files(), pace_of(4096), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        // getcfheaders: at most 2,000 filter hashes, both ends counted.
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(2000), input_prefix_type)));
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(1, c.hash(2001), input_prefix_type)));
        auto const most = co_await ask_compact_filter_headers(channels,
            headers_request(1, c.hash(2000), input_prefix_type));
        REQUIRE(most);
        CHECK(most->filter_hashes.size() == 2000);
        auto const last = co_await ask_compact_filter_headers(channels,
            headers_request(2, c.hash(2001), input_prefix_type));
        REQUIRE(last);
        CHECK(last->filter_hashes.size() == 2000);

        auto const checkpoint = co_await ask_compact_filter_checkpoint(channels,
            checkpoint_request(c.hash(2001), input_prefix_type));
        REQUIRE(checkpoint);
        CHECK(checkpoint->filter_headers.size() == 2001 / filtered_chain::interval);
    });
}

TEST_CASE("input prefix filter index drops the records of a branch the chain left",
    "[compact filters][input prefix][index]") {
    filtered_chain c("ipf_reorg", 108, 108, 101);
    auto& chain = c.fixture.chain();

    // A heavier branch off height 104. Its blocks spend the coinbases the
    // trunk's spend at the same heights, with other fees: other inputs.
    std::vector<domain::chain::block> branch;
    auto prev = c.hash(104);
    for (uint32_t h = 105; h <= 110; ++h) {
        branch.push_back(c.mine(prev, h, 2, 60));
        prev = branch.back().hash();
    }
    std::vector<domain::chain::block> on_branch;
    for (size_t h = 0; h <= 104; ++h) {
        on_branch.push_back(c.block(h));
    }
    on_branch.insert(on_branch.end(), branch.begin(), branch.end());
    auto const expected = records_of(on_branch, input_prefix_type);
    auto const expected_regular = records_of(on_branch);
    auto const on_trunk = records_of(c, 108, input_prefix_type);
    REQUIRE(expected[105].filter_hash != on_trunk[105].filter_hash);

    auto const switch_to_branch = [&] {
        auto const added = c.fixture.organizer().add_headers(test::headers_of(branch));
        REQUIRE(added.reorg_candidate);
        reorg_outcome reorg;
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(chain, c.fixture.organizer(), added.reorg_branch_head, 104,
                [] { return false; }, test::real_persister(chain)),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
        REQUIRE(reorg.result.ok);
        REQUIRE(chain.chain_view()->connected_tip_height == 104u);
        test::connect_bodies(c.fixture, branch, 105);
        REQUIRE(chain.chain_view()->tip_hash == branch.back().hash());
    };

    SECTION("while it runs") {
        with_index_of(c, c.both_files(), pace_of(256),
            [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            auto const before = co_await ask_compact_filter_headers(channels,
                headers_request(0, c.hash(108), input_prefix_type));
            REQUIRE(before);
            CHECK(before->filter_hashes == filter_hashes(on_trunk, 0, 108));

            switch_to_branch();
            // Asked before the ring: the trunk above the fork is not
            // answered, and neither is the branch, whose blocks have no record
            // yet, nor a branch block at a height the trunk had a record for.
            CHECK( ! co_await ask_compact_filter_headers(channels,
                headers_request(0, c.hash(108), input_prefix_type)));
            CHECK( ! co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash(), input_prefix_type)));
            CHECK( ! co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.front().hash(), input_prefix_type)));
            CHECK( ! co_await ask_compact_filter_checkpoint(channels,
                checkpoint_request(branch[3].hash(), input_prefix_type)));
            // Below the fork it is the same chain.
            CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(104), input_prefix_type)));

            channels.tip_moved();
            auto const after = co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash(), input_prefix_type));
            REQUIRE(after);
            CHECK(after->filter_hashes == filter_hashes(expected, 0, 110));
            auto const checkpoint = co_await ask_compact_filter_checkpoint(channels,
                checkpoint_request(branch.back().hash(), input_prefix_type));
            REQUIRE(checkpoint);
            REQUIRE(checkpoint->filter_headers.size() == 27);
            CHECK(checkpoint->filter_headers.back() == expected[108].filter_header);
            CHECK(expected[108].filter_header != on_trunk[108].filter_header);
            // The regular filter's records follow the same branch.
            auto const regular_after = co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash()));
            REQUIRE(regular_after);
            CHECK(regular_after->filter_hashes == filter_hashes(expected_regular, 0, 110));
        });
    }

    SECTION("while it does not") {
        with_index_of(c, c.both_files(), pace_of(256),
            [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(108), input_prefix_type)));
        });
        REQUIRE(stored_in(c.input_prefix_file).tip() == c.hash(108));

        switch_to_branch();
        // Both files name a block the chain left.
        with_index_of(c, c.both_files(), pace_of(256),
            [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
            auto const after = co_await ask_compact_filter_headers(channels,
                headers_request(0, branch.back().hash(), input_prefix_type));
            REQUIRE(after);
            CHECK(after->filter_hashes == filter_hashes(expected, 0, 110));
        });
    }

    auto const store = stored_in(c.input_prefix_file);
    REQUIRE(store.size() == 111);
    CHECK(store.tip() == branch.back().hash());
    for (size_t height = 0; height <= 110; ++height) {
        CAPTURE(height);
        CHECK(store.at(height) == expected[height]);
    }
    auto const regular_store = stored(c);
    REQUIRE(regular_store.size() == 111);
    CHECK(regular_store.at(110) == expected_regular[110]);
}

TEST_CASE("input prefix filter is served from the first block by a node started again on its database",
    "[compact filters][input prefix][index]") {
    filtered_chain c("ipf_existing", 108, 108, 101);
    auto const expected = records_of(c, 108, input_prefix_type);
    auto const regular = records_of(c, 108);

    // The node ran with the regular filter alone.
    with_index(c, pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        CHECK(co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(108))));
    });
    REQUIRE(stored(c).size() == 109);
    auto const regular_file = bytes_of(c.file);
    REQUIRE( ! std::filesystem::exists(c.input_prefix_file));

    // It starts again on the same database, with the input prefix filter on.
    // Four blocks a slice: the regular filter's records are loaded and
    // answered at once, and the input prefix filter's are computed from the
    // first block without holding them back.
    REQUIRE(c.fixture.restart());
    with_index_of(c, c.both_files(), pace_of(4), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const loaded = co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(108)));
        REQUIRE(loaded);
        CHECK(loaded->filter_hashes == filter_hashes(regular, 0, 108));
        auto const first = co_await ask_compact_filter_headers(channels,
            headers_request(0, c.hash(3), input_prefix_type));
        REQUIRE(first);
        CHECK(first->filter_hashes == filter_hashes(expected, 0, 3));
        CHECK( ! co_await ask_compact_filter_headers(channels, headers_request(0, c.hash(108), input_prefix_type)));
    });
    // The regular filter's file was taken as it was: not computed again, not
    // written again.
    CHECK(bytes_of(c.file) == regular_file);
    // The input prefix filter's file holds what the run computed before it
    // ended.
    auto const partial = stored_in(c.input_prefix_file).size();
    CHECK(partial >= 4);
    CHECK(partial < 109);

    // Started again: its records are taken up where they stopped, to the tip.
    REQUIRE(c.fixture.restart());
    with_index_of(c, c.both_files(), pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const whole = co_await ask_compact_filter_headers(channels,
            headers_request(0, c.hash(108), input_prefix_type));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 108));
    });
    auto const store = stored_in(c.input_prefix_file);
    REQUIRE(store.size() == 109);
    CHECK(store.tip() == c.hash(108));
    for (size_t height = 0; height <= 108; ++height) {
        CAPTURE(height);
        CHECK(store.at(height) == expected[height]);
    }
    CHECK(bytes_of(c.file) == regular_file);
}

TEST_CASE("input prefix filter index computes again the records of a file it cannot place",
    "[compact filters][input prefix][index]") {
    filtered_chain c("ipf_misplaced", 104, 103, 101);
    auto const expected = records_of(c, 103, input_prefix_type);

    // Records that chain, of blocks this node does not have.
    {
        compact_filter_store foreign(c.input_prefix_file);
        for (uint8_t i = 0; i < 20; ++i) {
            hash_digest hash{};
            hash[0] = i;
            foreign.append(hash);
        }
        REQUIRE( ! foreign.save(unknown_hash()));
    }
    with_index_of(c, c.both_files(), pace_of(256), [&](compact_filter_channels& channels) -> ::asio::awaitable<void> {
        auto const whole = co_await ask_compact_filter_headers(channels,
            headers_request(0, c.hash(103), input_prefix_type));
        REQUIRE(whole);
        CHECK(whole->filter_hashes == filter_hashes(expected, 0, 103));
    });
    auto const store = stored_in(c.input_prefix_file);
    REQUIRE(store.size() == 104);
    CHECK(store.tip() == c.hash(103));
    CHECK(store.at(103) == expected[103]);
}

TEST_CASE("the input prefix filter is off unless asked for, needs compact filters, and is refused on a pruning node",
    "[compact filters][input prefix][parser]") {
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.compact_filters", "true"};
        std::ostringstream error;
        REQUIRE(parser.parse(5, argv, error));
        CHECK( ! parser.configured.node.input_prefix_filters);
    }
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.compact_filters", "true",
            "--node.input_prefix_filters", "true"};
        std::ostringstream error;
        REQUIRE(parser.parse(7, argv, error));
        CHECK(parser.configured.node.compact_filters);
        CHECK(parser.configured.node.input_prefix_filters);
    }
    {
        // Alone: the wallets that ask for it look for the compact filters
        // service bit.
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.input_prefix_filters", "true"};
        std::ostringstream error;
        CHECK( ! parser.parse(5, argv, error));
        CHECK(error.str().find("node.input_prefix_filters needs node.compact_filters") != std::string::npos);
    }
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.compact_filters", "true",
            "--node.input_prefix_filters", "true", "--db.prune_keep_blocks", "288"};
        std::ostringstream error;
        CHECK( ! parser.parse(9, argv, error));
        CHECK(error.str().find("not allowed on a pruning node") != std::string::npos);
    }
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.input_prefix_filters", "true",
            "--db.prune_keep_blocks", "288"};
        std::ostringstream error;
        CHECK( ! parser.parse(7, argv, error));
    }
}

TEST_CASE("a node that serves the input prefix filter announces compact filters, and never while it prunes",
    "[compact filters][input prefix][node]") {
    auto const compact_filters = uint64_t(domain::message::version::service::node_compact_filters);
    auto const dir = std::filesystem::temp_directory_path() / ("kth_ipf_node_" + std::to_string(process_id()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    auto const config = [&dir](bool filters, bool input_prefix, uint32_t prune_keep_blocks) {
        node::configuration cfg{domain::config::network::regtest};
        cfg.database.directory = dir;
        cfg.database.db_max_size = 16ULL << 20;
        cfg.network.threads = 0;
        cfg.network.peers_file = dir / "peers.dat";
        cfg.network.hosts_file = dir / "hosts.dat";
        cfg.node.compact_filters = filters;
        cfg.node.input_prefix_filters = input_prefix;
        cfg.database.prune_keep_blocks = prune_keep_blocks;
        return cfg;
    };

    {
        // The bit of the compact filters, which says the regular filter is
        // served too.
        node::full_node node{config(true, true, 0)};
        CHECK((node.network_settings().services & compact_filters) != 0);
    }
    {
        node::full_node node{config(true, true, 288)};
        CHECK((node.network_settings().services & compact_filters) == 0);
    }
    {
        // Without the compact filters nothing is served.
        node::full_node node{config(false, true, 0)};
        CHECK((node.network_settings().services & compact_filters) == 0);
    }
    std::filesystem::remove_all(dir, ec);
}
