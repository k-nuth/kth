// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <future>
#include <string>
#include <thread>

#if defined(KTH_WITH_RPC)
#include <asio/ip/tcp.hpp>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>
#endif

#include "child_process_harness.hpp"

#include <kth/node/configuration.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/full_node.hpp>

using namespace kth;
using namespace kth::test;

// =============================================================================
// A node that stops on its own must end the process (#698)
// =============================================================================
//
// The process that produced this had already done everything right: the fault
// came from inside, the node stopped, joined, and said "Good bye!". Then it sat
// for five minutes holding four threads, until somebody sent it a SIGTERM.
//
// Nothing was broken in the teardown. What was missing is that the loop owning
// the process waited for exactly one thing -- an external signal -- and a node
// that ends by itself raises none. The executor's own threads are given back by
// stop(), and stop() was only ever called after that signal arrived.
//
// So the wait has to be for a signal OR the run finishing, and the second half
// has to be a fact rather than a guess: the run coroutine completing, published
// once, not `running()`, which reports a lifecycle state and says nothing about
// whether the work is over.

namespace {

node::configuration make_config(std::filesystem::path const& base) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = base / "chain";
    cfg.database.db_max_size = 64ULL << 20;
    cfg.network.threads = 1;
    cfg.network.outbound_connections = 0;
    cfg.network.inbound_connections = 0;
    cfg.network.inbound_port = 0;
    cfg.network.debug_file = base / "debug.log";
    cfg.network.error_file = base / "error.log";
    return cfg;
}

void make_startable_database(node::configuration const& cfg) {
    node::executor initialiser(cfg, false);
    REQUIRE(initialiser.do_initchain("internal shutdown control"));
}

// The loop main runs, reduced to what is under test. `signalled` stands in for
// the signal handler's atomic and is never set here: the whole point is that no
// signal arrives.
bool wait_for_signal_or_completion(node::executor& host,
                                   std::atomic<int> const& signalled,
                                   std::chrono::seconds patience) {
    auto const deadline = std::chrono::steady_clock::now() + patience;
    while (signalled.load() == 0) {
        if (host.wait_for_run_completion(std::chrono::milliseconds(100))) {
            return true;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
    }
    return false;
}

} // namespace

// -----------------------------------------------------------------------------

TEST_CASE("executor child: an internal fault ends the process without a signal",
          "[.executor-child]") {
    watchdog guard(full_start_limit, "internal fault ends the process");

    auto const base = claimed_dir("kth_exec_internal_end");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> never_signalled{0};

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == error::success);
        REQUIRE(host.running());

        // The fault comes from inside, the way a batch that cannot continue
        // reports one. No signal is raised, and none is simulated.
        auto const node = host.node();
        REQUIRE(node);
        node->notify_fatal("a control's internal fault");

        // The wait returns because the run finished, not because it timed out.
        // Under the defect this returns false after the whole patience: the node
        // has stopped and nothing tells the loop.
        CHECK(wait_for_signal_or_completion(host, never_signalled,
                                            std::chrono::seconds(120)));

        // And no signal was involved in any of it.
        CHECK(never_signalled.load() == 0);

        // What main does next, and what actually gives the threads back.
        host.stop();
        CHECK(host.stopped());
    }

    // The executor is destroyed here. Reaching this line at all is the claim: a
    // process still holding joinable threads would have been killed by the
    // watchdog above, and the parent checks for exactly that.
    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: an ordinary signal still ends the process",
          "[.executor-child]") {
    watchdog guard(full_start_limit, "a signal still ends the process");

    // The other half, and not a formality: a change that made the loop wait only
    // for internal completion would break every ordinary Ctrl-C, and would pass
    // the case above.
    auto const base = claimed_dir("kth_exec_signal_end");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> signalled{0};

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == error::success);

        std::thread signaller([&signalled]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            signalled.store(SIGTERM);
        });

        // The node is healthy and never completes on its own, so the only thing
        // that can end this wait is the signal.
        CHECK_FALSE(wait_for_signal_or_completion(host, signalled,
                                                  std::chrono::seconds(120)));
        CHECK(signalled.load() == SIGTERM);
        signaller.join();

        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// Parents
// -----------------------------------------------------------------------------

TEST_CASE("a node that stops on its own ends the process", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: an internal fault ends the process without a signal"));
}

TEST_CASE("a signal still ends the process", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: an ordinary signal still ends the process"));
}

#if defined(KTH_WITH_RPC)
TEST_CASE("executor child: stopping the network also joins RPC", "[.executor-child]") {
    watchdog guard(full_start_limit, "network-only stop joins RPC");
    auto const base = claimed_dir("kth_exec_network_rpc_end");
    auto cfg = make_config(base);
    ::asio::io_context reservation_context;
    ::asio::ip::tcp::acceptor reservation(reservation_context,
        {::asio::ip::make_address("127.0.0.1"), 0});
    cfg.rpc.port = reservation.local_endpoint().port();
    cfg.rpc.bind = "127.0.0.1";
    cfg.rpc.enabled = true;
    cfg.rpc.user = "test";
    cfg.rpc.password = "test";
    reservation.close();
    make_startable_database(cfg);

    // A readiness event, not a delay: stop only after the real RPC listener has
    // bound. The fixture's future is observation across the executor boundary.
    std::promise<void> listening;
    auto ready = listening.get_future();
    auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [&](spdlog::details::log_msg const& message) {
            std::string_view text(message.payload.data(), message.payload.size());
            if (text.find("JSON-RPC server listening") != std::string_view::npos)
                listening.set_value();
        });
    struct restore_logging {
        std::shared_ptr<spdlog::logger> previous = spdlog::default_logger();
        ~restore_logging() {
            spdlog::drop("rpc-network-stop-control");
            spdlog::set_default_logger(std::move(previous));
        }
    } restore;
    {
        node::executor host(cfg, false);
        // The executor constructor installs its own logger; observe after that.
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("rpc-network-stop-control", sink));
        REQUIRE(host.start() == error::success);
        auto const node = host.node();
        REQUIRE(node);
        bool const became_ready = ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
        bool completed = false;
        if (became_ready) {
            node->network().stop(); // Deliberately not full_node::stop().
            // Normal network shutdown itself takes about five seconds. The
            // failure bound must leave room for that measured path to finish.
            completed = host.wait_for_run_completion(std::chrono::seconds(15));
        }
        // Cleanup cannot change the observation above. Even RED joins all owned
        // work before the child returns, rather than stranding executor threads.
        host.stop();
        CHECK(became_ready);
        CHECK(completed);
    }
    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("stopping the network also joins RPC", "[executor][lifecycle][rpc]") {
    require_clean_child(run_child("executor child: stopping the network also joins RPC"));
}
#endif
