// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <kth/blockchain/prune.hpp>

using namespace kth;
using namespace kth::blockchain;
using kth::database::block_store;
using kth::database::header_index;
using kth::database::header_status;

// =============================================================================
// Choosing the block files a pruning node removes
// =============================================================================
//
// Driven with a real header index and a real store over real (empty) files:
// what is pinned is which files go for a given built height, and what the index
// says about the blocks that were in them.

namespace {

constexpr uint32_t blocks_per_file = 100;
constexpr uint32_t chain_length = 1000;     // heights 0 to 999, files 0 to 9
constexpr uint32_t keep = 288;

struct prune_chain {
    std::filesystem::path dir;
    header_index index{4096};
    std::vector<header_index::index_t> at;      // by height
    block_store store;

    explicit prune_chain(std::string const& name)
        : dir(std::filesystem::temp_directory_path() / ("kth_prune_" + name))
        , store(dir, block_store::magic_t{{0xe3, 0xe1, 0xf3, 0xe8}})
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);

        hash_digest parent = null_hash;
        for (uint32_t height = 0; height < chain_length; ++height) {
            auto const idx = add_header(parent, height);
            at.push_back(idx);
            parent = index.get_hash(idx);
            place(idx, int16_t(height / blocks_per_file));
        }

        for (int32_t file = 0; file < int32_t(chain_length / blocks_per_file); ++file) {
            touch(blk(file));
            touch(rev(file));
        }
        REQUIRE(store.initialize(block_store::numbering::pruned));
        auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
        REQUIRE(blocks.clean());
        auto const undo = store.scan_undo_positions(
            [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
        REQUIRE(undo.status == block_store::undo_scan_status::clean_eof);
    }

    ~prune_chain() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    header_index::index_t add_header(hash_digest const& parent, uint32_t salt) {
        domain::chain::header const header{1, parent, null_hash, salt * 600, 0x1d00ffff, salt};
        auto const added = index.add(domain::chain::hash(header), header);
        REQUIRE(added.inserted);
        return added.index;
    }

    // Give a block its data and undo in `file`.
    void place(header_index::index_t idx, int16_t file) {
        index.set_block_pos(idx, file, 8);
        index.add_status(idx, header_status::have_data);
        index.set_undo_pos(idx, 16);
        index.add_status(idx, header_status::have_undo);
    }

    std::filesystem::path blk(int32_t n) const { return dir / fmt::format("blk{:05d}.dat", n); }
    std::filesystem::path rev(int32_t n) const { return dir / fmt::format("rev{:05d}.dat", n); }

    static void touch(std::filesystem::path const& path) {
        FILE* file = std::fopen(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        std::fclose(file);
    }

    bool has_file(int32_t n) const {
        return std::filesystem::exists(blk(n)) && std::filesystem::exists(rev(n));
    }
    bool file_gone(int32_t n) const {
        return ! std::filesystem::exists(blk(n)) && ! std::filesystem::exists(rev(n));
    }
};

} // namespace

TEST_CASE("prune: the height below which data may go", "[prune]") {
    CHECK(prune_height(1000, 0) == 0u);         // keeps every block
    CHECK(prune_height(288, 288) == 0u);        // has not built more than it keeps
    CHECK(prune_height(100, 288) == 0u);
    CHECK(prune_height(289, 288) == 1u);
    CHECK(prune_height(1000, 288) == 712u);
}

TEST_CASE("prune: the first height that must have undo at start", "[prune]") {
    // A node that keeps every block: just above the checkpoint.
    CHECK(first_height_with_undo(500, 1000, 0) == 501u);
    // A pruning node whose window starts above the checkpoint.
    CHECK(first_height_with_undo(500, 1000, 288) == 713u);
    // A pruning node whose window still reaches below the checkpoint.
    CHECK(first_height_with_undo(900, 1000, 288) == 901u);
    // Full validation from genesis, nothing pruned yet.
    CHECK(first_height_with_undo(0, 200, 288) == 1u);
}

TEST_CASE("prune: files wholly below the window are removed and their blocks forgotten", "[prune]") {
    prune_chain c("window");

    // Built to 999, keeping 288: heights up to 711 may go. Height 712 is in
    // file 7, so files 0 to 6 are the ones wholly below the window.
    auto const pruned = prune_block_files(c.index, c.store, 999, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 7u);
    CHECK(pruned->first_file == 7);
    CHECK(pruned->blocks_forgotten == 700u);
    CHECK(c.store.first_file() == 7);
    for (int32_t file = 0; file < 7; ++file) CHECK(c.file_gone(file));
    for (int32_t file = 7; file < 10; ++file) CHECK(c.has_file(file));

    // The blocks of the removed files are not there for a reader any more.
    for (uint32_t height : {0u, 1u, 350u, 699u}) {
        auto const idx = c.at[height];
        CHECK_FALSE(c.index.has_block_data(idx));
        CHECK_FALSE(c.index.has_undo_data(idx));
        CHECK(c.index.get_block_pos(idx).is_null());
        CHECK_FALSE(c.index.has_status(idx, header_status::have_data));
        CHECK_FALSE(c.index.has_status(idx, header_status::have_undo));
    }

    // Everything in a kept file stays, including the blocks of file 7 that are
    // below the window: a file goes whole or not at all.
    for (uint32_t height : {700u, 711u, 712u, 999u}) {
        auto const idx = c.at[height];
        CHECK(c.index.has_block_data(idx));
        CHECK(c.index.has_undo_data(idx));
    }

    // Nothing else to remove at the same height.
    auto const again = prune_block_files(c.index, c.store, 999, keep);
    REQUIRE(again);
    CHECK(again->files_removed == 0u);
    CHECK(again->blocks_forgotten == 0u);
}

TEST_CASE("prune: a node that keeps every block removes nothing", "[prune]") {
    prune_chain c("keep_all");
    auto const pruned = prune_block_files(c.index, c.store, 999, 0);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 0u);
    for (int32_t file = 0; file < 10; ++file) CHECK(c.has_file(file));
    CHECK(c.index.has_block_data(c.at[0]));
}

TEST_CASE("prune: nothing goes until more blocks are built than are kept", "[prune]") {
    prune_chain c("young");
    auto const pruned = prune_block_files(c.index, c.store, keep, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 0u);
    CHECK(c.has_file(0));
}

TEST_CASE("prune: blocks not yet applied keep their files", "[prune]") {
    prune_chain c("ahead");

    // Stored to 999 but the UTXO set is built only to 500: heights up to 212
    // may go, which is files 0 and 1. Everything above is still to be applied
    // or inside the window.
    auto const pruned = prune_block_files(c.index, c.store, 500, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 2u);
    CHECK(c.store.first_file() == 2);
    CHECK(c.index.has_block_data(c.at[200]));
    CHECK(c.index.has_block_data(c.at[501]));
    CHECK(c.index.has_block_data(c.at[999]));
}

TEST_CASE("prune: a block inside the window keeps an old file and every file after it", "[prune]") {
    prune_chain c("pinned");

    // A block of the window that was written into file 2, as a block that
    // arrived late is. File 2 has to stay, and so do the files after it:
    // removal never leaves a gap.
    c.place(c.at[900], 2);

    auto const pruned = prune_block_files(c.index, c.store, 999, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 2u);
    CHECK(c.store.first_file() == 2);
    CHECK(c.file_gone(0));
    CHECK(c.file_gone(1));
    for (int32_t file = 2; file < 10; ++file) CHECK(c.has_file(file));
    CHECK(c.index.has_block_data(c.at[900]));
    CHECK(c.index.has_block_data(c.at[250]));
}

TEST_CASE("prune: a fork block above the window keeps its file", "[prune]") {
    prune_chain c("fork");

    // A competing block at height 951, stored in file 1. It is not on the
    // chain the node follows, and it is still a block a reorganization to that
    // branch would read.
    auto const fork = c.add_header(c.index.get_hash(c.at[950]), 5000);
    REQUIRE(c.index.get_height(fork) == 951);
    c.place(fork, 1);

    auto const pruned = prune_block_files(c.index, c.store, 999, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 1u);
    CHECK(c.store.first_file() == 1);
    CHECK(c.index.has_block_data(fork));
}

TEST_CASE("prune: with no block above the window nothing is removed", "[prune]") {
    prune_chain c("unknown");

    // The built height is far beyond every stored block, so no block says
    // where the files to keep begin.
    auto const pruned = prune_block_files(c.index, c.store, 5000, keep);
    REQUIRE(pruned);
    CHECK(pruned->files_removed == 0u);
    for (int32_t file = 0; file < 10; ++file) CHECK(c.has_file(file));
}
