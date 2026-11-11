// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>

#include <kth/database/databases/internal_database.hpp>
#include <kth/database/in_place_directory.hpp>
#include <kth/node.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using namespace kth;
using kth::database::in_place_directory;
using kth::database::prepare_in_place_directory;

namespace {

#if defined(__linux__)
bool has_nocow(std::filesystem::path const& path) {
    int const fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    int flags = 0;
    bool const ok = ::ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0;
    ::close(fd);
    return ok && (flags & FS_NOCOW_FL) != 0;
}

// Clears NOCOW on an empty directory, so what is created in it starts
// copy-on-write whatever the temporary directory itself carries.
void clear_nocow(std::filesystem::path const& path) {
    int const fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    int flags = 0;
    if (::ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0 && (flags & FS_NOCOW_FL) != 0) {
        flags &= ~FS_NOCOW_FL;
        (void)::ioctl(fd, FS_IOC_SETFLAGS, &flags);
    }
    ::close(fd);
}
#endif

// The tests' own root: copy-on-write, so its subdirectories do not inherit
// NOCOW from a temporary directory that has it.
std::filesystem::path const& cow_root() {
    static auto const root = [] {
        auto const dir = std::filesystem::temp_directory_path() / fmt::format("kth_in_place_{}", ::getpid());
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
#if defined(__linux__)
        clear_nocow(dir);
#endif
        return dir;
    }();
    return root;
}

std::filesystem::path fresh_dir(char const* tag) {
    auto const dir = cow_root() / tag;
    std::filesystem::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("prepare_in_place_directory creates a missing directory and marks it NOCOW on btrfs",
          "[database][in_place_directory]") {
    auto const dir = fresh_dir("fresh");
    auto const prepared = prepare_in_place_directory(dir);
    REQUIRE(prepared);
    CHECK(std::filesystem::is_directory(dir));
    if (*prepared == in_place_directory::not_btrfs || *prepared == in_place_directory::unsupported) {
        SUCCEED("not btrfs here: nothing to set");
    } else {
        REQUIRE(*prepared == in_place_directory::nocow_set);
#if defined(__linux__)
        CHECK(has_nocow(dir));
        // A file created in it inherits the attribute.
        std::ofstream(dir / "data") << "x";
        CHECK(has_nocow(dir / "data"));
#endif
        // A second call finds it already set.
        auto const again = prepare_in_place_directory(dir);
        REQUIRE(again);
        CHECK(*again == in_place_directory::nocow_already);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("a directory created inside a NOCOW one inherits it and is reported as such",
          "[database][in_place_directory]") {
    auto const parent = fresh_dir("parent");
    auto const prepared = prepare_in_place_directory(parent);
    REQUIRE(prepared);
    if (*prepared == in_place_directory::nocow_set) {
        auto const child = prepare_in_place_directory(parent / "child");
        REQUIRE(child);
        CHECK(*child == in_place_directory::nocow_already);
    } else {
        SUCCEED("not btrfs here: nothing is inherited");
    }
    std::filesystem::remove_all(parent);
}

TEST_CASE("prepare_in_place_directory leaves a directory that already holds files as it is",
          "[database][in_place_directory]") {
    auto const dir = fresh_dir("existing");
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "data") << "x";
    auto const prepared = prepare_in_place_directory(dir);
    REQUIRE(prepared);
    CHECK(*prepared != in_place_directory::nocow_set);
#if defined(__linux__)
    CHECK_FALSE(has_nocow(dir));
#endif
    std::filesystem::remove_all(dir);
}

TEST_CASE("a new LMDB store is created NOCOW on btrfs, and not when db.nocow is off",
          "[database][in_place_directory]") {
    for (bool const nocow : {true, false}) {
        auto const dir = fresh_dir(nocow ? "lmdb_on" : "lmdb_off");
        {
            database::internal_database db(dir, database::db_mode_type::blocks, 100, 64 * 1024 * 1024, false, nocow);
            REQUIRE(db.create());
            CHECK(db.close());
        }
        auto const probe = fresh_dir("probe");
        auto const fs = prepare_in_place_directory(probe);
        std::filesystem::remove_all(probe);
        REQUIRE(fs);
#if defined(__linux__)
        if (*fs == in_place_directory::nocow_set) {
            CHECK(has_nocow(dir) == nocow);
        }
#endif
        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("db.nocow defaults to true and is read from a configuration file",
          "[database][in_place_directory][parser]") {
    CHECK(database::settings{}.nocow);

    auto const dir = fresh_dir("cfg");
    std::filesystem::create_directories(dir);
    auto const path = dir / "kth.cfg";
    std::ofstream(path) << "db.nocow = false\n";
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parser.parse_from_file(path, error));
    CHECK_FALSE(parser.configured.database.nocow);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a new chain creates its UTXO-Z and LMDB stores NOCOW on btrfs",
          "[database][in_place_directory][blockchain]") {
    // The UTXO-Z directory is prepared by block_chain::start, before the store opens.
    // The fixture is created under the copy-on-write root, so its stores can only
    // be NOCOW because they were prepared, never by inheritance from TMPDIR.
    std::optional<std::string> const saved_tmpdir = [] () -> std::optional<std::string> {
        auto const* value = std::getenv("TMPDIR");
        return value ? std::optional<std::string>(value) : std::nullopt;
    }();
    ::setenv("TMPDIR", cow_root().c_str(), 1);
    test::chain_fixture fixture("nocow_chain");
    if (saved_tmpdir) ::setenv("TMPDIR", saved_tmpdir->c_str(), 1);
    else ::unsetenv("TMPDIR");
    REQUIRE(fixture.dir().parent_path() == cow_root());
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE(std::filesystem::is_directory(fixture.dir() / "utxoz"));
    auto const probe = fresh_dir("chain_probe");
    auto const fs = prepare_in_place_directory(probe);
    std::filesystem::remove_all(probe);
    REQUIRE(fs);
#if defined(__linux__)
    if (*fs == in_place_directory::nocow_set) {
        CHECK(has_nocow(fixture.dir() / "utxoz"));
        CHECK(has_nocow(fixture.dir() / "internal_db"));
        // blocks/ is not prepared, and its parent is copy-on-write.
        REQUIRE_FALSE(has_nocow(fixture.dir()));
        CHECK_FALSE(has_nocow(fixture.dir() / "blocks"));
    }
#endif
}

