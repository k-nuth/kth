// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The gRPC backend inside a real node: the executor starts a full_node over a
// real regtest chain database, the generated client calls it, and the node is
// stopped and joined. Each case runs in its own process (the child pattern the
// executor lifecycle tests use) because it owns process-wide logging and
// signal-free shutdown.

#include <test_helpers.hpp>

#include <filesystem>
#if ! defined(_WIN32)
#include <unistd.h>
#endif

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "child_process_harness.hpp"
#include "grpc_test_support.hpp"

#include <kth/node/configuration.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/full_node.hpp>

using namespace kth;
using namespace kth::test;
namespace v1 = ::kth::rpc::v1;

namespace {

node::configuration make_grpc_config(std::filesystem::path const& base) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = base / "chain";
    cfg.database.db_max_size = 64ULL << 20;
    cfg.network.threads = 1;
    cfg.network.outbound_connections = 0;
    cfg.network.inbound_connections = 0;
    cfg.network.inbound_port = 0;
    cfg.network.debug_file = base / "debug.log";
    cfg.network.error_file = base / "error.log";
    cfg.rpc.enabled = true;
    cfg.rpc.bind = "127.0.0.1";
    cfg.rpc.user = "node-user";
    cfg.rpc.password = "node-secret";
    return cfg;
}

std::uint16_t free_port() {
    ::asio::io_context context;
    ::asio::ip::tcp::acceptor probe(context, {::asio::ip::make_address("127.0.0.1"), 0});
    auto const port = probe.local_endpoint().port();
    probe.close();
    return port;
}

std::string bytes_of(hash_digest const& hash) {
    return std::string(reinterpret_cast<char const*>(hash.data()), hash.size());
}

void make_startable_database(node::configuration const& cfg) {
    node::executor initialiser(cfg, false);
    REQUIRE(initialiser.do_initchain("grpc full node"));
}

} // namespace

TEST_CASE("executor child: a started node serves its real chain over gRPC", "[.executor-child]") {
    watchdog guard(full_start_limit, "grpc serves the real chain");
    auto const base = claimed_dir("kth_grpc_node");
    auto cfg = make_grpc_config(base);
    cfg.rpc.port = free_port();
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == error::success);
        REQUIRE(host.running());

        node::test::client c(cfg.rpc.port, node::test::basic_auth("node-user", "node-secret"));
        auto const genesis = domain::chain::block::genesis_regtest();

        // The real block_chain heights and hash, through the chain executor.
        auto const info = c.chain_info();
        REQUIRE(info.status.ok());
        CHECK(info.response.network() == v1::NETWORK_REGTEST);
        CHECK(info.response.block_height() == 0);
        CHECK(info.response.header_height() == 0);
        CHECK(info.response.best_block_hash() == bytes_of(genesis.hash()));

        // Malformed input and credentials are refused by the real service.
        CHECK(c.block("short").status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        CHECK(node::test::client(cfg.rpc.port, node::test::basic_auth("node-user", "nope"))
            .chain_info().status.error_code() == grpc::StatusCode::UNAUTHENTICATED);

        // The real block_chain::fetch_block / fetch_transaction. At this revision
        // the by-hash lookups are stubs that report "not found" (see docs/grpc.md);
        // an implementation may answer OK with the genesis block instead.
        auto const block = c.block(bytes_of(genesis.hash()));
        CHECK((block.status.ok() || block.status.error_code() == grpc::StatusCode::NOT_FOUND));
        if (block.status.ok()) {
            CHECK(block.response.height() == 0);
        }
        CHECK(c.transaction(bytes_of(null_hash)).status.error_code() == grpc::StatusCode::NOT_FOUND);

        // Stop: the server is joined before the chain closes, and then answers no one.
        host.stop();
        CHECK(host.stopped());
        CHECK_FALSE(c.chain_info().status.ok());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a started node serves its real chain over gRPC", "[executor][lifecycle][grpc]") {
    require_clean_child(run_child("executor child: a started node serves its real chain over gRPC"));
}

TEST_CASE("executor child: a node refuses to start gRPC on a non-loopback bind", "[.executor-child]") {
    watchdog guard(full_start_limit, "grpc refuses non-loopback");
    auto const base = claimed_dir("kth_grpc_node_bind");
    auto cfg = make_grpc_config(base);
    cfg.rpc.port = free_port();
    cfg.rpc.bind = "0.0.0.0";
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        // Asked for, unsafe, therefore a failed start - not a node quietly
        // running without the service it was configured to offer.
        CHECK(host.start() == error::listen_failed);
        host.stop();
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a node refuses to start gRPC on a non-loopback bind", "[executor][lifecycle][grpc]") {
    require_clean_child(run_child("executor child: a node refuses to start gRPC on a non-loopback bind"));
}

TEST_CASE("executor child: stopping the network also joins gRPC", "[.executor-child]") {
    watchdog guard(full_start_limit, "network-only stop joins gRPC");
    auto const base = claimed_dir("kth_grpc_node_net_stop");
    auto cfg = make_grpc_config(base);
    cfg.rpc.port = free_port();
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == error::success);
        auto const node = host.node();
        REQUIRE(node);
        REQUIRE(node::test::client(cfg.rpc.port, node::test::basic_auth("node-user", "node-secret"))
            .chain_info().status.ok());

        // Embedders may stop P2P directly; run() must still finish, which needs the
        // gRPC server to be asked to stop by the same event.
        node->network().stop();
        CHECK(host.wait_for_run_completion(std::chrono::seconds(30)));
        host.stop();
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("stopping the network also joins gRPC", "[executor][lifecycle][grpc]") {
    require_clean_child(run_child("executor child: stopping the network also joins gRPC"));
}

#if ! defined(_WIN32)
TEST_CASE("executor child: a node that cannot save its private cookie does not start gRPC", "[.executor-child]") {
    watchdog guard(full_start_limit, "grpc fails closed without a private cookie");
    auto const base = claimed_dir("kth_grpc_node_cookie");
    auto cfg = make_grpc_config(base);
    cfg.rpc.port = free_port();
    cfg.rpc.user.clear();       // no configured credentials: the cookie is the credential
    cfg.rpc.password.clear();
    make_startable_database(cfg);

    // The working directory is where the cookie goes. Make it unwritable.
    auto const locked = base / "locked";
    std::filesystem::create_directory(locked);
    std::filesystem::permissions(locked,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    auto const previous = std::filesystem::current_path();
    std::filesystem::current_path(locked);
    {
        node::executor host(cfg, false);
        CHECK(host.start() == error::listen_failed);
        host.stop();
    }
    std::filesystem::current_path(previous);
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all);

    // No secret anywhere, and nothing is listening.
    CHECK_FALSE(std::filesystem::exists(locked / ".cookie"));
    CHECK_FALSE(node::test::client(cfg.rpc.port, node::test::basic_auth("__cookie__", "x"))
        .chain_info().status.ok());

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a node that cannot save its private cookie does not start gRPC", "[executor][lifecycle][grpc]") {
    if (::geteuid() == 0) {
        SKIP("root bypasses directory permissions");
    }
    require_clean_child(run_child("executor child: a node that cannot save its private cookie does not start gRPC"));
}
#endif
