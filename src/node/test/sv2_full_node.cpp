// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The Noise Template Provider inside a real node: the executor starts a
// full_node over a real regtest chain database. Each case runs in its own
// process, the child pattern of the executor lifecycle tests, because a node
// owns process-wide logging and shutdown.

#include <test_helpers.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if ! defined(_WIN32)
#include <unistd.h>
#endif

#include "child_process_harness.hpp"

#include <kth/node/configuration.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/full_node.hpp>

using namespace kth;
using namespace kth::test;

#if ! defined(_WIN32)

namespace {

node::configuration make_noise_config(std::filesystem::path const& base) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = base / "chain";
    cfg.database.db_max_size = 64ULL << 20;
    cfg.network.threads = 1;
    cfg.network.outbound_connections = 0;
    cfg.network.inbound_connections = 0;
    cfg.network.inbound_port = 0;
    cfg.network.debug_file = base / "debug.log";
    cfg.network.error_file = base / "error.log";
    cfg.sv2.enabled = true;
    cfg.sv2.port = 0;
    cfg.sv2.noise = true;
    return cfg;
}

void make_startable_database(node::configuration const& cfg) {
    node::executor initialiser(cfg, false);
    REQUIRE(initialiser.do_initchain("sv2 noise full node"));
}

std::string contents(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("executor child: a node asked for Noise with credentials it cannot use does not start", "[.executor-child]") {
    watchdog guard(full_start_limit, "sv2 noise refuses unusable credentials");
    auto const base = claimed_dir("kth_sv2_noise_refused");
    auto const good = make_noise_config(base);
    make_startable_database(good);
    auto const key = good.database.directory / "sv2_static_key";

    // Each is a node that would otherwise start and run with no listener.
    auto const refused = [&](node::configuration const& cfg) {
        node::executor host(cfg, false);
        CHECK(host.start() != error::success);
        host.stop();
    };
    {
        auto cfg = good;
        cfg.sv2.authority_public_key = "not an authority key";
        refused(cfg);
        // Refused before any key was made.
        CHECK_FALSE(std::filesystem::exists(key));
    }
    {
        auto cfg = good;
        cfg.sv2.certificate_validity_seconds = 0;
        refused(cfg);
        CHECK_FALSE(std::filesystem::exists(key));
    }
    {
        // A key file others can read is refused and left as it is.
        {
            std::ofstream output(key, std::ios::binary);
            output << std::string(32, '\x01');
        }
        std::filesystem::permissions(key, std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write | std::filesystem::perms::group_read,
            std::filesystem::perm_options::replace);
        refused(good);
        CHECK(contents(key) == std::string(32, '\x01'));
        std::filesystem::remove(key);
    }
    {
        // The same node with usable credentials starts.
        node::executor host(good, false);
        CHECK(host.start() == error::success);
        host.stop();
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a node asked for Noise with credentials it cannot use does not start", "[sv2 noise][executor]") {
    require_clean_child(run_child(
        "executor child: a node asked for Noise with credentials it cannot use does not start"));
}

TEST_CASE("executor child: a node keeps its Noise keys in its data directory", "[.executor-child]") {
    watchdog guard(full_start_limit, "sv2 noise keys in the data directory");
    auto const base = claimed_dir("kth_sv2_noise_keys");
    auto const cfg = make_noise_config(base);
    make_startable_database(cfg);
    auto const static_key = cfg.database.directory / "sv2_static_key";
    auto const authority_key = cfg.database.directory / "sv2_authority_key";

    auto const previous = std::filesystem::current_path();
    auto const started_from = [&](std::filesystem::path const& directory) {
        std::filesystem::create_directories(directory);
        std::filesystem::current_path(directory);
        node::executor host(cfg, false);
        CHECK(host.start() == error::success);
        host.stop();
        std::filesystem::current_path(previous);
    };
    started_from(base / "first");
    REQUIRE(std::filesystem::exists(static_key));
    REQUIRE(std::filesystem::exists(authority_key));
    auto const static_bytes = contents(static_key);
    auto const authority_bytes = contents(authority_key);
    CHECK(static_bytes.size() == 32);
    CHECK(authority_bytes.size() == 32);
    // Nothing in the directory the node was started from.
    CHECK(std::filesystem::is_empty(base / "first"));

    // Started from somewhere else, it is the same node: the same keys.
    started_from(base / "second");
    CHECK(contents(static_key) == static_bytes);
    CHECK(contents(authority_key) == authority_bytes);
    CHECK(std::filesystem::is_empty(base / "second"));

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a node keeps its Noise keys in its data directory", "[sv2 noise][executor]") {
    require_clean_child(run_child("executor child: a node keeps its Noise keys in its data directory"));
}

#endif
