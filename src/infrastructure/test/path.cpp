// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/path.hpp>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string_view>

TEST_CASE("files_size counts only immediate regular files", "[path]") {
    struct directory {
        kth::path value = fs::temp_directory_path() /
            ("kth-file-size-" + std::to_string(std::random_device{}()));
        directory() { if (!fs::create_directory(value)) throw std::runtime_error("temporary directory exists"); }
        ~directory() { std::error_code ec; fs::remove_all(value, ec); }
    } dir;
    REQUIRE(kth::files_size(dir.value) == 0);
    auto make_file = [](kth::path const& p, uint64_t length) {
        auto* file = std::fopen(p.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        REQUIRE(std::fclose(file) == 0);
        fs::resize_file(p, length);
    };
    make_file(dir.value / "blk.dat", 13);
    make_file(dir.value / "rev.dat", 7);
    make_file(dir.value / "empty", 0);
    fs::create_directory(dir.value / "subdir");
    make_file(dir.value / "subdir" / "excluded", 99);
    REQUIRE(kth::files_size(dir.value) == 20);
    // Preserve the best-effort contract when the directory is absent.
    REQUIRE(kth::files_size(dir.value / "missing") == 0);

    // A name filter selects which files count.
    REQUIRE(kth::files_size(dir.value, [](std::string_view name) { return name.starts_with("blk"); }) == 13);
    REQUIRE(kth::files_size(dir.value, [](std::string_view) { return false; }) == 0);
    REQUIRE(kth::files_size(dir.value / "missing", [](std::string_view) { return true; }) == 0);
}
