// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <kth/database/block_store.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// Reading a run of blocks up to a number of bytes
// =============================================================================

namespace {

constexpr block_store::magic_t test_magic{{0xe3, 0xe1, 0xf3, 0xe8}};

struct read_fixture {
    std::filesystem::path dir;
    std::optional<block_store> store;
    std::vector<flat_file_pos> positions;
    std::vector<data_chunk> blocks;

    // One stored block per size given, each filled with its own byte.
    read_fixture(std::string const& name, std::vector<size_t> const& sizes)
        : dir(std::filesystem::temp_directory_path() / ("kth_store_within_" + name))
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        store.emplace(dir, test_magic);
        REQUIRE(store->initialize());
        auto const scanned = store->scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
        REQUIRE(scanned.clean());
        auto const undo = store->scan_undo_positions(
            [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
        REQUIRE(undo.status == block_store::undo_scan_status::clean_eof);
        REQUIRE(store->append_enabled());

        for (size_t i = 0; i < sizes.size(); ++i) {
            blocks.emplace_back(sizes[i], uint8_t(i + 1));
            auto const pos = store->save_block_raw(blocks.back(), uint32_t(i), 0);
            REQUIRE_FALSE(pos.is_null());
            positions.push_back(pos);
        }
    }

    ~read_fixture() {
        store.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("block store read within: the run stops before the block that would pass the limit", "[block_store]") {
    read_fixture f("stops", {100, 200, 300, 400});

    auto const read = f.store->read_blocks_raw_within(f.positions, 650);
    REQUIRE(read);
    REQUIRE(read->size() == 3);     // 100 + 200 + 300 fit, 400 more would not
    CHECK((*read)[0] == f.blocks[0]);
    CHECK((*read)[1] == f.blocks[1]);
    CHECK((*read)[2] == f.blocks[2]);
}

TEST_CASE("block store read within: a run that adds up to the limit exactly is read whole", "[block_store]") {
    read_fixture f("exact", {100, 200, 300});

    auto const read = f.store->read_blocks_raw_within(f.positions, 600);
    REQUIRE(read);
    CHECK(read->size() == 3);

    auto const one_less = f.store->read_blocks_raw_within(f.positions, 599);
    REQUIRE(one_less);
    CHECK(one_less->size() == 2);
}

TEST_CASE("block store read within: the first block is read whatever its size", "[block_store]") {
    read_fixture f("first", {500, 100});

    auto const read = f.store->read_blocks_raw_within(f.positions, 10);
    REQUIRE(read);
    REQUIRE(read->size() == 1);
    CHECK((*read)[0] == f.blocks[0]);

    auto const none_allowed = f.store->read_blocks_raw_within(f.positions, 0);
    REQUIRE(none_allowed);
    CHECK(none_allowed->size() == 1);
}

TEST_CASE("block store read within: a limit above the whole run reads all of it", "[block_store]") {
    read_fixture f("all", {100, 200, 300});

    auto const read = f.store->read_blocks_raw_within(f.positions, uint64_t{1} << 40);
    REQUIRE(read);
    REQUIRE(read->size() == 3);
    CHECK(*read == f.blocks);
}

TEST_CASE("block store read within: no positions, no blocks", "[block_store]") {
    read_fixture f("none", {100});

    auto const read = f.store->read_blocks_raw_within({}, 1000);
    REQUIRE(read);
    CHECK(read->empty());
}

TEST_CASE("block store read within: a position with no block is an error", "[block_store]") {
    read_fixture f("missing", {100, 200});
    auto positions = f.positions;
    positions.push_back(flat_file_pos{});

    CHECK_FALSE(f.store->read_blocks_raw_within(positions, 1000));
    // Not reached, not read: the limit ends the run before the missing one.
    auto const before = f.store->read_blocks_raw_within(positions, 150);
    REQUIRE(before);
    CHECK(before->size() == 1);
}

TEST_CASE("block store read within: positions after the block that uses the limit up are not looked at", "[block_store]") {
    read_fixture f("spent", {100, 200});
    auto positions = f.positions;
    positions.push_back(flat_file_pos{});

    // The first block alone is over the limit.
    auto const oversized = f.store->read_blocks_raw_within({f.positions[1], flat_file_pos{}}, 10);
    REQUIRE(oversized);
    REQUIRE(oversized->size() == 1);
    CHECK((*oversized)[0] == f.blocks[1]);

    // The run adds up to the limit exactly.
    auto const exact = f.store->read_blocks_raw_within(positions, 300);
    REQUIRE(exact);
    CHECK(exact->size() == 2);

    // One byte more and the missing one is this call's to report.
    CHECK_FALSE(f.store->read_blocks_raw_within(positions, 301));
}

TEST_CASE("block store read within: sizes are read right for positions out of disk order", "[block_store]") {
    read_fixture f("order", {100, 200, 300, 400});
    std::vector<flat_file_pos> const positions{f.positions[3], f.positions[0], f.positions[2], f.positions[1]};

    auto const read = f.store->read_blocks_raw_within(positions, 800);     // 400 + 100 + 300
    REQUIRE(read);
    REQUIRE(read->size() == 3);
    CHECK((*read)[0] == f.blocks[3]);
    CHECK((*read)[1] == f.blocks[0]);
    CHECK((*read)[2] == f.blocks[2]);
}
