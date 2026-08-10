// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/database/block_store.hpp>
#include <kth/database/durability.hpp>
#include <kth/database/native_file.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// The durability barriers under a chain transition (#600)
// =============================================================================
//
// What was there before: a `flush` returning void, discarding both results it
// got, covering only the last block file, and called from nowhere in the repo.
// A batch that crosses a file rotation writes undo into more than one rev*.dat,
// so "the last one" was never the right set — and nothing asked for even that.
//
// These tests are about the three things that make the new barrier a barrier: it
// covers every file it was given, it says which one failed, and what it cannot
// promise it reports rather than answering success.

namespace {

struct store_fixture {
    std::filesystem::path dir;
    block_store store;

    explicit store_fixture(std::string const& name)
        : dir(std::filesystem::temp_directory_path() / ("kth_undo_barrier_" + name))
        , store(dir, block_store::magic_t{{0xe3, 0xe1, 0xf3, 0xe8}})
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        REQUIRE(store.initialize());
    }

    ~store_fixture() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    store_fixture(store_fixture const&) = delete;
    store_fixture& operator=(store_fixture const&) = delete;
};

} // namespace

TEST_CASE("the undo barrier covers a single file", "[undo_barriers]") {
    store_fixture fixture("single");

    std::vector<int32_t> const files{0};
    auto const flushed = fixture.store.flush_undo(files);
    CHECK(flushed.has_value());
}

TEST_CASE("the undo barrier normalizes what it is given", "[undo_barriers]") {
    // One number per block arrives here, so a thousand-block batch inside one
    // file asks for a thousand barriers on it. Duplicates are the normal case,
    // not an abuse.
    store_fixture fixture("dedup");

    std::vector<int32_t> const files(1000, 0);
    auto const flushed = fixture.store.flush_undo(files);
    CHECK(flushed.has_value());
}

TEST_CASE("the undo barrier reports which file failed", "[undo_barriers]") {
    // The file number is carried in the error rather than left in a log line:
    // a caller deciding whether the node can continue needs to name what did
    // not reach the disk, and a log is the one place that answer cannot be
    // acted on.
    store_fixture fixture("names_file");

    std::vector<int32_t> const files{0, 4242};
    auto const flushed = fixture.store.flush_undo(files);

    REQUIRE_FALSE(flushed.has_value());
    CHECK(flushed.error().file_number == 4242);
    CHECK(flushed.error().code == result_code::other);
}

TEST_CASE("the undo barrier does not stop at the last file", "[undo_barriers]") {
    // The control against the shape this replaces. `flush` synced only
    // `last_block_file_`; an implementation that still did that would never
    // look at a file that sorts before it, and would report success here.
    //
    // -5 cannot be flushed and sorts first, so the answer is a failure naming
    // it. A last-file-only barrier answers success, and a barrier that reported
    // the LAST failure rather than the first would name a different file.
    store_fixture fixture("not_last_only");

    std::vector<int32_t> const files{-5, 0};
    auto const flushed = fixture.store.flush_undo(files);

    REQUIRE_FALSE(flushed.has_value());
    CHECK(flushed.error().file_number == -5);
}

TEST_CASE("the undo barrier stops at the first failure", "[undo_barriers]") {
    // Not "keeps going and reports the last": a partial barrier is not a weaker
    // guarantee, it is no guarantee, and the caller cannot act on "some of it
    // reached the disk" any differently than on "none of it did". Reporting the
    // first is what makes the file number worth carrying.
    store_fixture fixture("first_failure");

    std::vector<int32_t> const files{9000, 9001, 9002};
    auto const flushed = fixture.store.flush_undo(files);

    REQUIRE_FALSE(flushed.has_value());
    CHECK(flushed.error().file_number == 9000);
}

TEST_CASE("an empty set of files is not a failure", "[undo_barriers]") {
    // A transition that wrote no undo — a reorganization only reads it — still
    // asks for the barrier, and asking for nothing has to be answerable.
    store_fixture fixture("empty");

    std::vector<int32_t> const files;
    auto const flushed = fixture.store.flush_undo(files);
    CHECK(flushed.has_value());
}

TEST_CASE("the directory barrier is reported, not assumed", "[undo_barriers]") {
    // Making a file's contents durable does not make the name that reaches them
    // durable. Where the platform has that barrier the store says so; where it
    // does not, it says that instead of reporting a guarantee it cannot keep.
    store_fixture fixture("directory");

    auto const barrier = fixture.store.directory_durability();

#ifdef _WIN32
    CHECK(barrier == directory_barrier::unsupported);
#else
    CHECK(barrier == directory_barrier::available);

    // And it actually runs on a real directory.
    auto const [reported, ok] = sync_directory(fixture.dir);
    CHECK(reported == directory_barrier::available);
    CHECK(ok);
#endif
}

TEST_CASE("a directory that cannot be opened is a failure, not a pass",
          "[undo_barriers]") {
    // The failure direction that matters. A barrier that answers "fine" when it
    // could not run is the defect this whole change is about, one level down.
#ifndef _WIN32
    auto const missing = std::filesystem::temp_directory_path() /
        "kth_undo_barrier_missing_directory";
    std::error_code ec;
    std::filesystem::remove_all(missing, ec);

    auto const [reported, ok] = sync_directory(missing);
    CHECK(reported == directory_barrier::available);
    CHECK_FALSE(ok);
#endif
}

TEST_CASE("the node's durability is the weakest of the barriers it depends on",
          "[undo_barriers]") {
    // KTH's own guarantee, not any one store's. A caller that read UTXO-Z's
    // `full` as the node's would be claiming a barrier the rev directory may
    // not have.
    auto const level = node_durability_level();

    bool const known = level == durability_level::full
                    || level == durability_level::contents_only
                    || level == durability_level::none;
    REQUIRE(known);

    // Never stronger than what the UTXO store reports for the same machine.
    auto const utxoz_level = utxoz::platform_durability();
    if (utxoz_level == utxoz::durability_level::none) {
        CHECK(level == durability_level::none);
    }
    if (utxoz_level == utxoz::durability_level::contents_only) {
        CHECK((level == durability_level::contents_only || level == durability_level::none));
    }

#ifdef _WIN32
    // No directory barrier there, whatever the stores say about contents.
    CHECK(level != durability_level::full);
#else
    // Not a tautology: on the platforms KTH ships a node for, all four barriers
    // exist. If this ever fails, something below stopped promising what the
    // transition protocol is built on, and that is worth finding out here.
    CHECK(level == durability_level::full);
#endif
}
