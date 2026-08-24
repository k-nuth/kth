// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <filesystem>
#include <string>
#include <system_error>
#include <chrono>
#include <thread>

#include "child_process_harness.hpp"

#include <kth/node/detail/p2p_node_test_seam.hpp>
#include <kth/node/p2p_node.hpp>

using namespace kth;
using namespace kth::test;

// =============================================================================
// A stop ends the status wait instead of waiting it out (#689)
// =============================================================================
//
// p2p_node logs a peer-count line every ten seconds, and it waited on a timer
// that lived inside that task's own coroutine frame — reachable by nobody. Its
// exit condition was re-read only when the timer expired on its own.
//
// The task is detached and nothing awaits it directly, but join() waits for the
// pool it keeps alive, so a stop could sit through up to ten seconds with
// nothing left to do. Measured at roughly five seconds on one lifecycle case,
// where it also contaminated a wall-clock control for an unrelated component.
//
// Checked through a probe rather than a clock. The shutdown window is delayed by
// more than this task, so a timing bound here would measure the others: that is
// exactly how a wall-clock control for the executor's heartbeat passed on Linux
// and failed on macOS.

namespace {

using probe_point = node::detail::p2p_node_test_seam::probe_point;

node::settings quiet_settings(std::filesystem::path const& dir) {
    node::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 1;   // the status task runs on this path
    settings.threads = 2;
    settings.hosts_file = (dir / "hosts_nonexistent.dat").string();
    settings.banlist_file = (dir / "banlist_nonexistent.dat").string();
    return settings;
}

// Bounded, so a task that never arms fails here rather than hanging the suite.
constexpr auto arm_limit = std::chrono::seconds(30);

} // namespace

TEST_CASE("p2p child: a stop ends an armed wait exactly once", "[.p2p-child]") {
    // In a CHILD, with a watchdog. The property under test is that a stop ends a
    // wait — and the way it fails is that it does not, leaving join() blocked
    // forever. An in-process deadline cannot interrupt a blocked join, and an
    // in-process watchdog that hard-exits would take the whole suite down with
    // it. Here the watchdog turns the deadlock into a diagnostic exit of this
    // process, and the parent reports it as a failure of this case alone.
    watchdog guard(std::chrono::seconds(120), "p2p stop ends an armed wait");

    auto const dir = claimed_dir("kth_p2p_status");
    auto settings = quiet_settings(dir);
    node::p2p_node network(settings);

    std::atomic<unsigned> armed{0};
    std::atomic<unsigned> woken{0};
    std::atomic<unsigned> reported{0};

    node::detail::p2p_node_test_seam::set_status_probe(network,
        [&armed, &woken, &reported](probe_point point) {
            switch (point) {
                case probe_point::armed:    ++armed;    break;
                case probe_point::woken:    ++woken;    break;
                case probe_point::reported: ++reported; break;
            }
        });

    ::asio::io_context ctx;
    ::asio::co_spawn(ctx, [&network]() -> ::asio::awaitable<void> {
        auto const ec = co_await network.start();
        if (ec) {
            co_return;
        }
        co_await network.run();
    }, ::asio::detached);

    std::thread driver([&ctx] { ctx.run_for(std::chrono::seconds(90)); });

    // OBSERVED, not assumed. A stop that lands before the timer is armed ends the
    // loop on its own condition — correct, and not what this case measures.
    auto const deadline = std::chrono::steady_clock::now() + arm_limit;
    while (armed.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto const was_armed = armed.load();

    // Teardown BEFORE the assertions, and unconditionally: an assertion that
    // fires while the driver is still joinable leaves this process to end with a
    // running thread, which reports as a crash rather than as the failure it is.
    network.stop();
    network.join();
    ctx.stop();
    if (driver.joinable()) {
        driver.join();
    }

    REQUIRE(was_armed >= 1u);

    // The wait was ended by the stop, once.
    CHECK(woken.load() == 1u);

    // And no status line came out of it: an abort is not a report, and the run is
    // far shorter than one ten-second period, so a report here would mean the
    // cancellation had been counted as an ordinary tick.
    CHECK(reported.load() == 0u);

    // Nothing re-armed after the stop.
    CHECK(armed.load() == 1u);

    announce_completion();

    std::error_code rm;
    std::filesystem::remove_all(dir, rm);
}

// A node started and run, with its probe installed. The counters outlive it on
// purpose: a probe that fires during destruction must find them alive, and a
// residual task would show up here rather than as a crash.
struct running_node {
    std::atomic<unsigned> before_arm{0};
    std::atomic<unsigned> armed{0};
    std::atomic<unsigned> woken{0};
    std::atomic<unsigned> reported{0};

    // Declared AFTER the counters, so it is destroyed BEFORE them: the probe
    // captures them by reference and can fire from the destructor's own teardown.
    std::filesystem::path dir;
    node::settings settings;
    std::optional<node::p2p_node> network;

    ::asio::io_context ctx;
    std::thread driver;

    explicit running_node(char const* stem, std::function<void()> on_before_arm = {})
        : dir(claimed_dir(stem))
        , settings(quiet_settings(dir))
    {
        network.emplace(settings);
        node::detail::p2p_node_test_seam::set_status_probe(*network,
            [this, on_before_arm](probe_point point) {
                switch (point) {
                    case probe_point::before_arm:
                        ++before_arm;
                        if (on_before_arm) { on_before_arm(); }
                        break;
                    case probe_point::armed:    ++armed;    break;
                    case probe_point::woken:    ++woken;    break;
                    case probe_point::reported: ++reported; break;
                }
            });
    }

    void run() {
        ::asio::co_spawn(ctx, [this]() -> ::asio::awaitable<void> {
            auto const ec = co_await network->start();
            if (ec) {
                co_return;
            }
            co_await network->run();
        }, ::asio::detached);
        driver = std::thread([this] { ctx.run_for(std::chrono::seconds(90)); });
    }

    void wait_until(std::atomic<unsigned> const& counter, unsigned at_least) {
        auto const deadline = std::chrono::steady_clock::now() + arm_limit;
        while (counter.load() < at_least && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void settle() {
        ctx.stop();
        if (driver.joinable()) {
            driver.join();
        }
    }

    ~running_node() {
        network.reset();
        settle();
        std::error_code rm;
        std::filesystem::remove_all(dir, rm);
    }
};

TEST_CASE("p2p child: a stop before the timer arms leaves it unarmed", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p stop before arming");

    // Deterministic, not "we stopped quickly". The probe BLOCKS the task at the
    // point before it arms; the stop happens while it is parked there; then it is
    // released and must find `stopped_` and end without arming.
    //
    // start(); stop(); with no run() would not test this: it would only say the
    // task never existed.
    std::atomic<bool> parked{false};
    std::atomic<bool> release{false};

    running_node node("kth_p2p_before_arm", [&parked, &release] {
        parked.store(true);
        while ( ! release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    node.run();

    // Parked at the point before arming.
    auto const deadline = std::chrono::steady_clock::now() + arm_limit;
    while ( ! parked.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto const was_parked = parked.load();

    // The stop lands while it is parked...
    node.network->stop();
    release.store(true);

    node.network->join();
    node.settle();

    REQUIRE(was_parked);

    // ...and it never armed.
    CHECK(node.armed.load() == 0u);
    CHECK(node.woken.load() == 0u);
    CHECK(node.reported.load() == 0u);

    announce_completion();
}

TEST_CASE("p2p child: stopping twice wakes once and re-arms nothing", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p repeated stop");

    running_node node("kth_p2p_twice");
    node.run();
    node.wait_until(node.armed, 1);
    REQUIRE(node.armed.load() >= 1u);

    node.network->stop();
    node.network->stop();       // the second one has no timer left to cancel
    node.network->join();
    node.settle();

    CHECK(node.woken.load() == 1u);
    CHECK(node.armed.load() == 1u);
    CHECK(node.reported.load() == 0u);

    announce_completion();
}

TEST_CASE("p2p child: destroying a running node completes", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p destroy while running");

    // No explicit stop: the destructor is the teardown, and it has to complete
    // rather than wait out a ten-second period. The watchdog is what makes that
    // an assertion instead of a hang.
    {
        running_node node("kth_p2p_running_dtor");
        node.run();
        node.wait_until(node.armed, 1);
        REQUIRE(node.armed.load() >= 1u);
    }

    announce_completion();
}

TEST_CASE("p2p child: a node that never ran leaves nothing behind", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p never started");

    {
        running_node node("kth_p2p_never_ran");
        // No run(), so no status task: nothing arms, nothing is woken, nothing
        // reports, and the destructor still comes down cleanly.
    }

    announce_completion();
}

TEST_CASE("p2p child: nothing probes after the teardown", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p no residual task");

    // The counters outlive the node — see running_node — so a probe firing during
    // or after destruction is counted rather than crashing. Read after everything
    // is down: a residual task would keep moving these.
    unsigned settled_armed = 0;
    unsigned settled_woken = 0;
    unsigned settled_reported = 0;

    {
        running_node node("kth_p2p_residual");
        node.run();
        node.wait_until(node.armed, 1);
        REQUIRE(node.armed.load() >= 1u);

        node.network->stop();
        node.network->join();
        node.settle();

        settled_armed = node.armed.load();
        settled_woken = node.woken.load();
        settled_reported = node.reported.load();

        // Nothing moves after the join.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(node.armed.load() == settled_armed);
        CHECK(node.woken.load() == settled_woken);
        CHECK(node.reported.load() == settled_reported);
    }

    announce_completion();
}

TEST_CASE("a stop before the p2p timer arms leaves it unarmed", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: a stop before the timer arms leaves it unarmed"));
}

TEST_CASE("stopping a p2p node twice wakes once", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: stopping twice wakes once and re-arms nothing"));
}

TEST_CASE("destroying a running p2p node completes", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: destroying a running node completes"));
}

TEST_CASE("a p2p node that never ran leaves nothing behind", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: a node that never ran leaves nothing behind"));
}

TEST_CASE("no p2p probe fires after the teardown", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: nothing probes after the teardown"));
}

TEST_CASE("a stop ends an armed p2p status wait", "[node][p2p][status_timer]") {
    require_clean_child(run_child("p2p child: a stop ends an armed wait exactly once"));
}

// =============================================================================
// Single-use, in code rather than by convention
// =============================================================================
//
// start() used to consult only `stopped_`, which goes back to true on every stop.
// So start-stop-start was ADMITTED: it returned success and then ran over
// channels stop() had closed and a pool join() had ended. "Restart is broken" is
// not "a second run cannot happen" — the sequence was reachable through the
// public API, and everything built on its absence rested on the caller choosing
// not to try.

TEST_CASE("p2p single-use - a refused start launches nothing, even if ignored",
    "[node][p2p][single_use]") {

    auto const dir = claimed_dir("kth_p2p_single_use");
    auto settings = quiet_settings(dir);
    node::p2p_node network(settings);

    std::atomic<unsigned> armed{0};
    std::atomic<unsigned> reported{0};
    node::detail::p2p_node_test_seam::set_status_probe(network,
        [&armed, &reported](probe_point point) {
            if (point == probe_point::armed) { ++armed; }
            if (point == probe_point::reported) { ++reported; }
        });

    ::asio::io_context ctx;
    code first{error::success};
    code second{error::success};
    code ran{error::success};
    std::atomic<int> done{0};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        first = co_await network.start();
        ++done;

        // A clean stop, and then another start. This is the sequence the old
        // code accepted: `stopped_` is true again, so its only gate was open.
        network.stop();

        second = co_await network.start();
        ++done;

        // And the caller IGNORES the refusal and runs anyway, which is the whole
        // point of this case. Checking the probe after a refused start() alone
        // would prove nothing: the status task lives in run(), not in start(),
        // so it would be silent whether or not the refusal happened.
        ran = co_await network.run();
        ++done;
        co_return;
    }, ::asio::detached);

    ctx.run_for(std::chrono::seconds(60));
    REQUIRE(done.load() == 3);

    CHECK(first == error::success);
    CHECK(second == error::operation_failed);

    // run() refuses too, rather than starting a second set of tasks over closed
    // channels.
    CHECK(ran == error::service_stopped);

    network.stop();
    network.join();

    // Nothing armed and nothing reported: no second status task was launched.
    CHECK(armed.load() == 0u);
    CHECK(reported.load() == 0u);

    std::error_code rm;
    std::filesystem::remove_all(dir, rm);
}

TEST_CASE("p2p single-use - two admissions from two threads produce exactly one winner",
    "[node][p2p][single_use]") {

    // A REAL race, on two threads with an io_context each. Two coroutines on one
    // context would not be one: start() reaches the admission before its first
    // suspension, so they serialise themselves.
    //
    // What this pins is the OUTCOME — one winner, one refusal — and NOT the
    // atomicity of the admission. Stated because it was measured: replacing the
    // exchange with a load, a yield and a store leaves this green, three runs out
    // of three. That says the interleaving did not come up, not that it cannot.
    //
    // So this is a positive check and a stress case, with that limit declared.
    // What fixes the correctness is the single atomic operation itself, and what
    // pins the property that matters is the case above: an admission once spent
    // stays spent across a clean stop, which goes red without the exchange.
    auto const dir = claimed_dir("kth_p2p_single_use_race");
    auto settings = quiet_settings(dir);
    node::p2p_node network(settings);

    std::atomic<unsigned> admitted{0};
    std::atomic<unsigned> refused{0};
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    auto racer = [&] {
        ::asio::io_context ctx;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            ++ready;
            while ( ! go.load()) {
                std::this_thread::yield();
            }
            auto const ec = co_await network.start();
            if (ec) {
                ++refused;
            } else {
                ++admitted;
            }
            co_return;
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(60));
    };

    std::thread a(racer);
    std::thread b(racer);

    // Both parked on the same line before either can admit.
    auto const deadline = std::chrono::steady_clock::now() + arm_limit;
    while (ready.load() != 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Published unconditionally, and both threads joined, BEFORE anything fatal:
    // a REQUIRE that fires here would leave two joinable threads whose
    // destructors terminate the process, reporting as a crash rather than as the
    // failure it is.
    auto const both_ready = ready.load() == 2;
    go.store(true);

    a.join();
    b.join();

    REQUIRE(both_ready);

    network.stop();
    network.join();

    // Exactly one, whichever got there first. An exchange guarantees it; a read
    // followed by a write does not.
    CHECK(admitted.load() == 1u);
    CHECK(refused.load() == 1u);

    std::error_code rm;
    std::filesystem::remove_all(dir, rm);
}

TEST_CASE("p2p child: hammering start against stop keeps the invariant", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p stop racing the admission");

    // THE race this serialisation exists for. The admission and the active state
    // used to be two separate atomics:
    //
    //   1. start() takes the admission
    //   2. stop() lands here, reads the INITIAL `stopped_ == true`, concludes
    //      there is nothing to stop, and returns
    //   3. start() publishes `stopped_ = false`
    //   4. the node is running, and the stop that was meant to end it has
    //      already come back
    //
    // A POSITIVE STRESS CASE, and not a discriminating control. Say so, because
    // the difference matters: splitting the transition back into a load, a yield
    // and a store survived 200 attempts here and failed once at attempt 1163 of
    // 1500. That is evidence the mutation CAN show, gathered by hand — not a
    // guarantee it will show on another machine, another scheduler, or under a
    // sanitizer that reorders things in either direction.
    //
    // What the correctness rests on is the single compare-exchange, the single
    // exchange, and the two ordered cases below, which are deterministic. This
    // one hammers the invariant and would notice a gross regression; it is not
    // what proves the property.
    auto const dir = claimed_dir("kth_p2p_stop_race");
    auto settings = quiet_settings(dir);

    for (int attempt = 0; attempt != 200; ++attempt) {
        node::p2p_node network(settings);

        ::asio::io_context ctx;
        std::atomic<bool> go{false};
        std::atomic<int> done{0};
        code started{error::success};

        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            while ( ! go.load()) {
                std::this_thread::yield();
            }
            started = co_await network.start();
            ++done;
            co_return;
        }, ::asio::detached);

        std::thread stopper([&] {
            while ( ! go.load()) {
                std::this_thread::yield();
            }
            network.stop();
            ++done;
        });

        std::thread driver([&ctx] { ctx.run_for(std::chrono::seconds(30)); });
        go.store(true);

        auto const deadline = std::chrono::steady_clock::now() + arm_limit;
        while (done.load() != 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto const both_done = done.load() == 2;

        // READ BEFORE THE CLEANUP. The cleanup stop below would set this flag
        // itself, so asking afterwards answers about the cleanup rather than
        // about the race — which is how the first version of this case passed
        // over a build with the defect fully restored.
        auto const left_running = ! network.stopped();

        network.stop();
        network.join();
        ctx.stop();
        if (stopper.joinable()) { stopper.join(); }
        if (driver.joinable()) { driver.join(); }

        REQUIRE(both_done);

        // The invariant: whichever order they landed in, a stop that returned
        // cannot leave this node running. Either the start was refused because
        // the stop got there first, or the stop took effect on a started node.
        // What must never happen is an admitted start whose `stopped_ = false`
        // outlived a stop that had already returned.
        CAPTURE(attempt);
        CHECK_FALSE(left_running);
    }

    announce_completion();
}

TEST_CASE("hammering a p2p start against a stop keeps the invariant", "[node][p2p][single_use]") {
    require_clean_child(run_child("p2p child: hammering start against stop keeps the invariant"));
}

// The two orders of the one transition, deterministic. Between them they say
// what the state machine is for, without depending on a scheduler landing a
// few-instruction window.

TEST_CASE("p2p lifecycle - a stop that arrives first refuses the start",
    "[node][p2p][single_use]") {

    // `fresh → stopped`. Nothing was ever running, so the stop has no work to do
    // — and the claim still counts: the start that follows finds something other
    // than `fresh` and is refused. A stop that was merely forgotten because there
    // was nothing to stop would let that start through.
    auto const dir = claimed_dir("kth_p2p_stop_first");
    auto settings = quiet_settings(dir);
    node::p2p_node network(settings);

    std::atomic<unsigned> armed{0};
    node::detail::p2p_node_test_seam::set_status_probe(network,
        [&armed](probe_point point) {
            if (point == probe_point::armed) { ++armed; }
        });

    REQUIRE(network.stopped());
    network.stop();
    CHECK(network.stopped());

    ::asio::io_context ctx;
    code started{error::success};
    bool ran = false;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        started = co_await network.start();
        ran = true;
        co_return;
    }, ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));

    REQUIRE(ran);
    CHECK(started == error::operation_failed);

    // Refused, so nothing was ever active and nothing armed.
    CHECK(network.stopped());
    CHECK(armed.load() == 0u);

    network.join();

    std::error_code rm;
    std::filesystem::remove_all(dir, rm);
}

TEST_CASE("p2p lifecycle - a start that arrives first is torn down by the stop",
    "[node][p2p][single_use]") {

    // `fresh → active → stopped`. The other order: the start wins the transition,
    // so the stop finds `active` and has work to do rather than concluding there
    // was nothing running.
    auto const dir = claimed_dir("kth_p2p_start_first");
    auto settings = quiet_settings(dir);
    node::p2p_node network(settings);

    ::asio::io_context ctx;
    code started{error::operation_failed};
    bool ran = false;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        started = co_await network.start();
        ran = true;
        co_return;
    }, ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));

    REQUIRE(ran);
    REQUIRE(started == error::success);

    // Admitted, so it is NOT stopped: `active` is published by the start itself.
    CHECK_FALSE(network.stopped());

    network.stop();
    CHECK(network.stopped());

    // And the lifecycle is spent: a start after the stop is refused too.
    code again{error::success};
    bool ran_again = false;
    ctx.restart();
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        again = co_await network.start();
        ran_again = true;
        co_return;
    }, ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));

    REQUIRE(ran_again);
    CHECK(again == error::operation_failed);

    network.join();

    std::error_code rm;
    std::filesystem::remove_all(dir, rm);
}

TEST_CASE("p2p child: run is admitted once and spawns one status task", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p run admitted once");

    // run() spawns every network task, the status one among them. A second run()
    // over an active node would put a SECOND coroutine on the one status_timer_,
    // and then a cancellation ends whichever of them happens to be waiting —
    // leaving the other asleep for its full ten seconds, which is the delay this
    // whole change removes.
    running_node node("kth_p2p_run_once");

    ::asio::io_context ctx;
    code first{error::operation_failed};
    code second{error::success};
    std::atomic<int> done{0};

    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        auto const started = co_await node.network->start();
        if (started) {
            ++done;
            co_return;
        }
        ::asio::co_spawn(co_await ::asio::this_coro::executor,
            [&]() -> ::asio::awaitable<void> {
                first = co_await node.network->run();
                ++done;
                co_return;
            }, ::asio::detached);
        co_return;
    }, ::asio::detached);

    std::thread driver([&ctx] { ctx.run_for(std::chrono::seconds(90)); });

    // Wait until the first run() has the status task armed, so the second one
    // below is genuinely a second run over an active node.
    auto const deadline = std::chrono::steady_clock::now() + arm_limit;
    while (node.armed.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto const was_armed = node.armed.load() >= 1u;

    ::asio::io_context second_ctx;
    ::asio::co_spawn(second_ctx, [&]() -> ::asio::awaitable<void> {
        second = co_await node.network->run();
        co_return;
    }, ::asio::detached);
    second_ctx.run_for(std::chrono::seconds(30));

    // Read BEFORE the teardown, which would arm nothing but would end the task.
    auto const armed_after = node.armed.load();

    node.network->stop();
    node.network->join();
    ctx.stop();
    if (driver.joinable()) { driver.join(); }

    REQUIRE(was_armed);

    // Refused, and no second status task: exactly one arming, ever.
    CHECK(second == error::operation_failed);
    CHECK(armed_after == 1u);

    announce_completion();
}

TEST_CASE("a second p2p run is refused", "[node][p2p][single_use]") {
    require_clean_child(run_child("p2p child: run is admitted once and spawns one status task"));
}

TEST_CASE("p2p child: two runs at once leave exactly one admitted", "[.p2p-child]") {
    watchdog guard(std::chrono::seconds(120), "p2p concurrent run admission");

    // Sequential repetition is covered above; this is the ordering question. The
    // property comes from the single exchange, not from catching an interleaving,
    // so there is no probabilistic mutation here: what this adds is that two
    // callers arriving together still leave one admitted, and TSan watching it.
    //
    // The winner does not return until the node stops — run() IS the work — so
    // the loser's refusal is what arrives first.
    running_node node("kth_p2p_run_race");

    ::asio::io_context starter;
    ::asio::co_spawn(starter, [&]() -> ::asio::awaitable<void> {
        auto const ec = co_await node.network->start();
        REQUIRE_FALSE(ec);
        co_return;
    }, ::asio::detached);
    starter.run_for(std::chrono::seconds(30));

    std::atomic<unsigned> refused{0};
    std::atomic<unsigned> returned{0};
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    auto racer = [&] {
        ::asio::io_context ctx;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            ++ready;
            while ( ! go.load()) {
                std::this_thread::yield();
            }
            auto const ec = co_await node.network->run();
            if (ec == error::operation_failed) {
                ++refused;
            }
            ++returned;
            co_return;
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(60));
    };

    std::thread a(racer);
    std::thread b(racer);

    auto const deadline = std::chrono::steady_clock::now() + arm_limit;
    while (ready.load() != 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto const both_ready = ready.load() == 2;
    go.store(true);

    // The loser refuses immediately; the winner is inside the node until it is
    // stopped, so stop first and then join both.
    auto const refused_deadline = std::chrono::steady_clock::now() + arm_limit;
    while (refused.load() == 0 && std::chrono::steady_clock::now() < refused_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto const refused_one = refused.load();

    node.network->stop();
    node.network->join();

    a.join();
    b.join();

    REQUIRE(both_ready);

    // Exactly one refusal: one caller was admitted and did the work, the other
    // was told so rather than spawning a second set of tasks.
    CHECK(refused_one == 1u);
    CHECK(refused.load() == 1u);
    CHECK(returned.load() == 2u);

    announce_completion();
}

TEST_CASE("two concurrent p2p runs leave one admitted", "[node][p2p][single_use]") {
    require_clean_child(run_child("p2p child: two runs at once leave exactly one admitted"));
}
