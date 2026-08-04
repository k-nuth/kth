// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>

#include <kth/node/configuration.hpp>
#include <kth/node/full_node.hpp>

using namespace kth;

// =============================================================================
// Reporting a condition the node cannot go on from
// =============================================================================
//
// Sync discovers such a condition from inside the node's own tasks (a switch
// whose persisted description could not be written — see reorg_outcome::fatal),
// but ending the process belongs to whoever owns the node. full_node is the
// hinge: it stops itself and hands the reason on.
//
// What is pinned here is the handing on: that the reason reaches the owner,
// unchanged, and that a node without a handler is still safe to report to.
//
// Neither side of it is. Above, the coordinator's `if (reorg.fatal) on_fatal(…)`
// is not exercised: the reorg tests call execute_reorg directly and read the
// flag themselves, which is not the same as watching the coordinator act on it.
// Below, the executor's stop path — state to stopping, the heartbeat ending,
// join() — needs a running node with a network that no test stands up. Both
// halves are wired by inspection only, and saying otherwise here would make a
// passing run look like more than it is.
//
// Not pinned: that notify_fatal stops the node. A node that never started
// already reports stopped(), so on an unstarted one that assertion would hold
// before the call as much as after — it would look like evidence and be none.

namespace {

std::filesystem::path make_dir() {
    auto const p = std::filesystem::temp_directory_path() /
        ("kth_fatal_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

node::configuration make_config(std::filesystem::path const& dir) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = dir;
    cfg.database.db_max_size = 16ULL << 20;   // 16 MiB, as the runtime example uses
    cfg.network.threads = 0;                  // no network threads: nothing is started here
    return cfg;
}

} // namespace

TEST_CASE("a fatal condition reaches the node's owner verbatim", "[node][fatal]") {
    auto const dir = make_dir();
    node::full_node node{make_config(dir)};

    std::string reported;
    int calls = 0;
    node.set_fatal_handler([&](std::string const& reason) { reported = reason; ++calls; });

    node.notify_fatal("the persisted chain and the active chain describe different branches");

    // Once, and verbatim: the reason is what the owner logs and what an operator
    // has to act on, so it must arrive as it was written rather than as a class
    // of error the caller has to guess at.
    CHECK(calls == 1);
    CHECK(reported == "the persisted chain and the active chain describe different branches");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("reporting a fatal condition with no handler installed is safe", "[node][fatal]") {
    // Embedding the node as a library without installing a handler is allowed:
    // notify_fatal still stops the node, and only the telling of it is optional.
    // Reporting must therefore not depend on someone having listened.
    auto const dir = make_dir();
    node::full_node node{make_config(dir)};

    REQUIRE_NOTHROW(node.notify_fatal("no handler installed"));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
