// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

#include <kth/database/block_store.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// Removing the oldest block files
// =============================================================================
//
// A pruned store has no files below some number. These drive block_store
// against real files: what is pinned is which directories it accepts, and what
// a removal leaves on disk.

namespace {

using numbering = block_store::numbering;

struct prune_fixture {
    std::filesystem::path dir;

    explicit prune_fixture(std::string const& name)
        : dir(std::filesystem::temp_directory_path() / ("kth_store_prune_" + name))
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
    }

    ~prune_fixture() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    std::filesystem::path blk(int32_t n) const { return dir / fmt::format("blk{:05d}.dat", n); }
    std::filesystem::path rev(int32_t n) const { return dir / fmt::format("rev{:05d}.dat", n); }

    static void touch(std::filesystem::path const& path) {
        FILE* file = std::fopen(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        std::fclose(file);
    }

    // Block files `from` to `to`, each with its undo file.
    void make_files(int32_t from, int32_t to) const {
        for (auto n = from; n <= to; ++n) {
            touch(blk(n));
            touch(rev(n));
        }
    }

    bool has(int32_t n) const { return std::filesystem::exists(blk(n)); }
    bool has_undo(int32_t n) const { return std::filesystem::exists(rev(n)); }
};

constexpr block_store::magic_t test_magic{{0xe3, 0xe1, 0xf3, 0xe8}};

// What production does after initialize(): both walks, which on empty files
// read nothing and are clean.
void walk(block_store& store) {
    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    REQUIRE(blocks.clean());
    auto const undo = store.scan_undo_positions(
        [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
    REQUIRE(undo.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(store.append_enabled());
}

} // namespace

TEST_CASE("block store prune: the oldest files are removed with their undo", "[block_store][prune]") {
    prune_fixture f("remove");
    f.make_files(0, 3);
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    walk(store);
    CHECK(store.first_file() == 0);

    auto const removed = store.remove_files_below(2);
    REQUIRE(removed);
    CHECK(*removed == 2u);
    CHECK(store.first_file() == 2);
    CHECK_FALSE(f.has(0));
    CHECK_FALSE(f.has_undo(0));
    CHECK_FALSE(f.has(1));
    CHECK_FALSE(f.has_undo(1));
    CHECK(f.has(2));
    CHECK(f.has_undo(2));
    CHECK(f.has(3));

    // Asking again for what is already gone removes nothing.
    auto const again = store.remove_files_below(2);
    REQUIRE(again);
    CHECK(*again == 0u);
}

TEST_CASE("block store prune: the file being written is never removed", "[block_store][prune]") {
    prune_fixture f("last");
    f.make_files(0, 2);
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    walk(store);

    CHECK_FALSE(store.remove_files_below(3));
    CHECK(f.has(0));
    CHECK(f.has(2));

    // Up to the last file is allowed, and leaves exactly that one.
    auto const removed = store.remove_files_below(2);
    REQUIRE(removed);
    CHECK(*removed == 2u);
    CHECK(f.has(2));
}

TEST_CASE("block store prune: a pruned directory opens again and keeps its numbering",
          "[block_store][prune]") {
    prune_fixture f("reopen");
    f.make_files(0, 3);
    {
        block_store store(f.dir, test_magic);
        REQUIRE(store.initialize(numbering::pruned));
        walk(store);
        REQUIRE(store.remove_files_below(2));
    }
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    CHECK(store.first_file() == 2);
    walk(store);
    // The walks must not bring the removed files back.
    CHECK_FALSE(f.has(0));
    CHECK_FALSE(f.has_undo(0));
    CHECK_FALSE(f.has(1));
    CHECK_FALSE(f.has_undo(1));
}

TEST_CASE("block store prune: missing first files are refused unless pruning is expected",
          "[block_store][prune]") {
    prune_fixture f("unexpected");
    f.make_files(2, 3);
    block_store store(f.dir, test_magic);
    CHECK_FALSE(store.initialize());
    CHECK_FALSE(store.initialize(numbering::complete));
    CHECK(store.initialize(numbering::pruned));
}

TEST_CASE("block store prune: a file missing between two is refused under either numbering",
          "[block_store][prune]") {
    prune_fixture f("gap");
    f.make_files(2, 5);
    std::filesystem::remove(f.blk(4));
    block_store store(f.dir, test_magic);
    CHECK_FALSE(store.initialize(numbering::pruned));
    CHECK_FALSE(store.initialize(numbering::complete));
}

TEST_CASE("block store prune: flushing undo does not bring a removed file back", "[block_store][prune]") {
    prune_fixture f("flush");
    f.make_files(0, 3);
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    walk(store);
    REQUIRE(store.remove_files_below(2));

    std::array<int32_t, 1> const removed_file{0};
    CHECK_FALSE(store.flush_undo(removed_file));
    CHECK_FALSE(f.has_undo(0));

    // A file the store still holds is flushed as before.
    std::array<int32_t, 1> const kept_file{2};
    CHECK(store.flush_undo(kept_file));
}

TEST_CASE("block store prune: a leftover undo file is taken even when no block file is removed",
          "[block_store][prune]") {
    prune_fixture f("leftover_only");
    f.make_files(1, 1);
    // A removal stopped after blk00000.dat and before rev00000.dat.
    prune_fixture::touch(f.rev(0));
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    CHECK(store.first_file() == 1);
    walk(store);

    auto const removed = store.remove_files_below(1);
    REQUIRE(removed);
    CHECK(*removed == 0u);
    CHECK_FALSE(f.has_undo(0));
    CHECK(f.has(1));
    CHECK(f.has_undo(1));
}

TEST_CASE("block store prune: an undo file left below the first block file is taken by the next removal",
          "[block_store][prune]") {
    prune_fixture f("leftover");
    f.make_files(1, 3);
    // A removal stopped after blk00000.dat and before rev00000.dat.
    prune_fixture::touch(f.rev(0));
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    CHECK(store.first_file() == 1);
    walk(store);

    auto const removed = store.remove_files_below(2);
    REQUIRE(removed);
    CHECK(*removed == 1u);
    CHECK_FALSE(f.has_undo(0));
    CHECK_FALSE(f.has_undo(1));
    CHECK(f.has_undo(2));
}

TEST_CASE("block store prune: a leftover undo file that cannot be removed is reported and tried again",
          "[block_store][prune]") {
    prune_fixture f("leftover_denied");
    f.make_files(1, 1);
    prune_fixture::touch(f.rev(0));
    block_store store(f.dir, test_magic);
    REQUIRE(store.initialize(numbering::pruned));
    walk(store);

    // A directory that cannot be written to refuses the removal.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::permissions(f.dir, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
    REQUIRE( ! ec);
    struct restore_permissions {
        fs::path dir;
        ~restore_permissions() {
            std::error_code ignored;
            fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ignored);
        }
    } const restore{f.dir};

    auto const denied = store.remove_files_below(1);
    if (denied) {
        SKIP("this process cannot be denied a removal in a directory it owns (running as root, "
             "or a file system without permissions)");
    }
    CHECK(f.has_undo(0));

    // With the directory writable again the same call finishes the job: the
    // failed search was not counted as done.
    fs::permissions(f.dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    REQUIRE( ! ec);
    auto const retried = store.remove_files_below(1);
    REQUIRE(retried);
    CHECK(*retried == 0u);
    CHECK_FALSE(f.has_undo(0));
}
