// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if ! defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <kth/node/grpc_rpc/server.hpp>

using namespace kth::node::grpc_rpc;
namespace fs = std::filesystem;

namespace {

struct scratch {
    scratch() {
        for (unsigned i = 0;; ++i) {
            dir = fs::temp_directory_path() / ("kth_grpc_cookie_" + std::to_string(i));
            std::error_code ec;
            if (fs::create_directory(dir, ec)) break;
            REQUIRE_FALSE(ec);
        }
    }
    ~scratch() {
        std::error_code ec;
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

std::string slurp(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> names(fs::path const& dir) {
    std::vector<std::string> out;
    for (auto const& entry : fs::directory_iterator(dir)) out.push_back(entry.path().filename().string());
    return out;
}

} // namespace

TEST_CASE("the private file holds exactly the secret and leaves nothing else behind", "[grpc cookie]") {
    scratch s;
    auto const path = s.dir / ".cookie";
    REQUIRE(write_private_file(path, "__cookie__:abc").has_value());
    CHECK(slurp(path) == "__cookie__:abc");
    CHECK(names(s.dir) == std::vector<std::string>{".cookie"});

    // A second write replaces the first.
    REQUIRE(write_private_file(path, "__cookie__:def").has_value());
    CHECK(slurp(path) == "__cookie__:def");
    CHECK(names(s.dir) == std::vector<std::string>{".cookie"});

#if ! defined(_WIN32)
    struct stat info;
    REQUIRE(::stat(path.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);   // whatever the umask says
#endif
}

#if ! defined(_WIN32)
TEST_CASE("the private file ignores a permissive umask", "[grpc cookie]") {
    scratch s;
    auto const old = ::umask(0);
    auto const written = write_private_file(s.dir / ".cookie", "secret");
    ::umask(old);
    REQUIRE(written.has_value());
    struct stat info;
    REQUIRE(::stat((s.dir / ".cookie").c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
}

TEST_CASE("an existing link at the cookie path is replaced and never written through", "[grpc cookie]") {
    scratch s;
    auto const victim = s.dir / "victim.txt";
    { std::ofstream(victim) << "do not touch"; }
    fs::permissions(victim, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read
        | fs::perms::others_read);
    auto const cookie = s.dir / ".cookie";
    fs::create_symlink(victim, cookie);

    REQUIRE(write_private_file(cookie, "__cookie__:abc").has_value());

    CHECK(slurp(victim) == "do not touch");
    CHECK_FALSE(fs::is_symlink(cookie));
    CHECK(slurp(cookie) == "__cookie__:abc");
    struct stat info;
    REQUIRE(::stat(victim.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0644);   // its permissions were not changed either
}

TEST_CASE("a dangling link and a link to a directory are replaced too", "[grpc cookie]") {
    scratch s;
    fs::create_symlink(s.dir / "nowhere", s.dir / ".cookie");
    REQUIRE(write_private_file(s.dir / ".cookie", "x").has_value());
    CHECK_FALSE(fs::exists(s.dir / "nowhere"));
    CHECK(slurp(s.dir / ".cookie") == "x");
}

TEST_CASE("an unwritable directory fails closed and leaves nothing", "[grpc cookie]") {
    if (::geteuid() == 0) {
        SKIP("root bypasses directory permissions");
    }
    scratch s;
    auto const locked = s.dir / "locked";
    fs::create_directory(locked);
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec);
    auto const result = write_private_file(locked / ".cookie", "secret");
    fs::permissions(locked, fs::perms::owner_all);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("cannot create temporary file") != std::string::npos);
    CHECK(names(locked).empty());
}
#endif

TEST_CASE("a missing directory and an obstructing directory fail closed", "[grpc cookie]") {
    scratch s;
    CHECK_FALSE(write_private_file(s.dir / "absent" / ".cookie", "secret").has_value());

    // A directory (not a link) already named .cookie cannot be replaced by a file.
    fs::create_directory(s.dir / ".cookie");
    fs::create_directory(s.dir / ".cookie" / "keep");
    auto const result = write_private_file(s.dir / ".cookie", "secret");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("cannot replace") != std::string::npos);
    CHECK(fs::is_directory(s.dir / ".cookie" / "keep"));
    CHECK(names(s.dir) == std::vector<std::string>{".cookie"});   // temporary removed
}
