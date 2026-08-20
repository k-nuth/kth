// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#if ! defined(_WIN32)
#include <sys/wait.h>
#endif

#include <kth/node/configuration.hpp>
#include <kth/node/detail/executor_test_seam.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/full_node.hpp>

using namespace kth;

// The lifecycle's observable points. The enum is private to `executor`; the test
// seam is the friend that publishes it.
using probe_point = node::detail::executor_test_seam::probe_point;

// =============================================================================
// Destroying an executor whose start failed (#669)
// =============================================================================
//
// A start that fails is an ordinary outcome — start() reports it and logs the
// reason — but the object was not destructible afterwards. start_async() creates
// the io thread BEFORE it knows whether the node can start, and the destructor
// tore down only when state_ said `running`. A failed start leaves `starting`, so
// ~std::thread ran on a joinable thread: terminate, SIGABRT, a core file, and an
// operator left to decide whether the node crashed or refused.
//
// WHY THESE RUN IN A CHILD PROCESS
//
// The defect's signature IS abnormal termination. A control that exercised it in
// this process would take the suite down with it and report nothing, so each case
// below is a hidden Catch2 test that a parent case runs as a subprocess and reads
// the exit status of. Fixed, the child returns 0. Defective, it dies on a signal
// and prints "terminate called without an active exception" — and the parent says
// which of the two happened rather than vanishing alongside it.
//
// WHY THERE IS A WATCHDOG
//
// The other way to get this wrong is to join a thread that never ends. After a
// failed start the io context has no work left and the work guard is the only
// thing keeping run() from returning, so a teardown that joins without releasing
// it hangs forever instead of aborting. A hang in CI is a timeout with no
// diagnosis, so each child carries an RAII watchdog that hard-exits with a
// distinct code. It is not how a passing run ends: the healthy cases end by
// their own progress and a normal join, and the watchdog is destroyed without
// ever having fired.

namespace {

// -----------------------------------------------------------------------------
// A temporary directory this process owns
// -----------------------------------------------------------------------------
//
// Claimed rather than named. create_directory() reports whether it was THIS call
// that created the directory, so the first name that comes back true is a name no
// concurrent run can also be using — which a name built from a pid is not, across
// containers that number pids alike.

std::filesystem::path claimed_dir(std::string_view stem) {
    auto const base = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt != 4096; ++attempt) {
        auto const candidate = base / (std::string(stem) + "_" + std::to_string(attempt));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
        REQUIRE_FALSE(ec);  // "exists" is not an error here; anything else is
    }
    FAIL("no temporary directory could be claimed");
    return {};
}

// -----------------------------------------------------------------------------
// Watchdog
// -----------------------------------------------------------------------------
//
// Waits on a condition variable rather than sleeping, so the destructor returns
// as soon as the work it guards is done — it costs a passing run nothing. When it
// does fire the process is wedged by definition, so it says so on stderr and
// leaves by _Exit: unwinding would need the very thread that is stuck.

constexpr int watchdog_exit_code = 97;

// Set well above any honest run rather than close to it. This is not a
// performance assertion: a case that takes longer than expected is not what it
// reports, a case that is never going to finish is. Sanitizer builds run these
// an order of magnitude slower, and a limit tight enough to trip on one of those
// would cost a red CI run for no defect at all.
constexpr auto teardown_limit = std::chrono::seconds(240);
constexpr auto full_start_limit = std::chrono::seconds(600);

class watchdog {
public:
    watchdog(std::chrono::milliseconds limit, char const* what)
        : thread_([this, limit, what]() {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_for(lock, limit, [this]() { return done_; })) {
                return;
            }
            std::fprintf(stderr, "WATCHDOG: '%s' did not finish in %lld ms\n",
                what, static_cast<long long>(limit.count()));
            std::fflush(stderr);
            std::_Exit(watchdog_exit_code);
        })
    {}

    watchdog(watchdog const&) = delete;
    watchdog& operator=(watchdog const&) = delete;

    ~watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{false};
    std::thread thread_;
};

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// The log files go in `base`, NOT in the database directory. The constructor
// opens them, and opening a file creates the directory holding it — inside the
// database directory that is enough to make do_initchain() report it as already
// existing and refuse, which is a fixture failing, not the thing under test.
node::configuration make_config(std::filesystem::path const& base) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = base / "chain";
    cfg.database.db_max_size = 64ULL << 20;
    cfg.network.threads = 1;
    cfg.network.outbound_connections = 0;   // no peers: nothing here needs a network
    cfg.network.inbound_connections = 0;
    cfg.network.inbound_port = 0;
    cfg.network.debug_file = base / "debug.log";
    cfg.network.error_file = base / "error.log";
    return cfg;
}

// Printed by a child that reached the end of its case, and required by the parent
// that ran it. Exiting zero is not enough on its own: a filter that matches
// nothing, a case that is skipped, a binary that does nothing at all — all of
// those also fail to print a "terminate" line, and without this they would read
// as passes.
constexpr char const* child_completed = "CHILD-COMPLETED";

void announce_completion() {
    std::puts(child_completed);
    std::fflush(stdout);
}

// -----------------------------------------------------------------------------
// Running one of the hidden cases below as a subprocess
// -----------------------------------------------------------------------------

// What a child did, in terms the failure message can print.
//
// std::system's value is platform-encoded, and on POSIX it is encoded twice
// over: an ordinary exit of 42 arrives as 10752. A control that printed that
// raw number would be reporting the encoding rather than the outcome, and the
// difference between "exited 97" (the watchdog) and "died on signal 6" (the
// defect) is exactly what these cases exist to tell apart.
struct child_run {
    int raw_status{-1};
    bool exited{false};      ///< it returned from main rather than dying
    int code{-1};            ///< its exit status, when it exited
    int signal{0};           ///< the signal that ended it, when one did
    std::string out;
    std::string err;

    [[nodiscard]] bool exited_cleanly() const {
        return exited && code == 0;
    }

    [[nodiscard]] std::string describe() const {
        if (signal != 0) {
            return "died on signal " + std::to_string(signal);
        }
        if (exited && code > 128) {
            return "exited " + std::to_string(code) + " (a shell reporting signal "
                + std::to_string(code - 128) + ")";
        }
        if (exited) {
            return "exited " + std::to_string(code);
        }
        return "ended in a way this platform does not describe (raw "
            + std::to_string(raw_status) + ")";
    }
};

// std::system returns the exit status directly on Windows and a wait status on
// POSIX. Neither is the other, so neither is compared against a bare integer
// anywhere below.
//
// The direct child is a shell — the command carries redirections — so a case
// that dies on a signal reaches here as the shell's ordinary exit of 128 + N.
// `signal` therefore stays 0 in practice; `code` is what carries the news, and
// 134 is the SIGABRT this file is largely about.
child_run decode_status(int raw) {
    child_run result;
    result.raw_status = raw;
#if defined(_WIN32)
    result.exited = true;
    result.code = raw;
#else
    if (WIFEXITED(raw)) {
        result.exited = true;
        result.code = WEXITSTATUS(raw);
    } else if (WIFSIGNALED(raw)) {
        result.signal = WTERMSIG(raw);
    }
#endif
    return result;
}

std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

child_run run_child(char const* case_name) {
    auto const dir = claimed_dir("kth_exec_child");
    auto const out = dir / "stdout.txt";
    auto const err = dir / "stderr.txt";

    std::string command;
    command += "\"";
    command += KTH_NODE_TEST_BINARY;
    command += "\" \"";
    command += case_name;
    command += "\" > \"";
    command += out.string();
    command += "\" 2> \"";
    command += err.string();
    command += "\"";

#if defined(_WIN32)
    // cmd.exe strips the outermost pair of quotes from a /c command line when it
    // begins with one, which would take the quoting off the binary's path and
    // break any path with a space in it. The documented answer is a second
    // outer pair for it to remove. Not exercised by CI, which runs the tests on
    // Linux and macOS only.
    command = "\"" + command + "\"";
#endif

    auto result = decode_status(std::system(command.c_str()));
    result.out = read_file(out);
    result.err = read_file(err);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return result;
}

// What every child must look like: it returned normally, and it did not die the
// way the defect dies.
void require_clean_child(child_run const& run) {
    INFO("child " << run.describe());
    INFO("child stdout:\n" << run.out);
    INFO("child stderr:\n" << run.err);
    // It did not die the way the defect dies,
    CHECK(run.signal == 0);
    CHECK(run.err.find("terminate called without an active exception") == std::string::npos);
    // nor the way a teardown that joins a thread still waiting for work dies,
    CHECK(run.code != watchdog_exit_code);
    CHECK(run.err.find("WATCHDOG") == std::string::npos);
    // it ran the case through to its last statement,
    CHECK(run.out.find(child_completed) != std::string::npos);
    // and it left normally.
    REQUIRE(run.exited_cleanly());
}

// -----------------------------------------------------------------------------
// The two failing starts, as fixtures
// -----------------------------------------------------------------------------
//
// Two causes, deliberately far apart in the start sequence, so that what the
// controls pin is the executor's teardown and not one particular way of failing.

// A database directory that exists and holds no database. verify_directory() is
// satisfied by its existence, so nothing initializes it, and LMDB fails to open
// before the blockchain is ever consulted. This is the shape a datadir has after
// a half-finished copy or a wiped volume.
void make_unopenable_database(node::configuration const& cfg) {
    std::error_code ec;
    std::filesystem::create_directories(cfg.database.directory, ec);
    REQUIRE_FALSE(ec);
}

// A database that opens, and block files that cannot be read back. The scan that
// governs the append cursor refuses rather than reporting a shorter answer
// (#668/#670), so blockchain::start() returns false — later than the case above,
// by a different route, through the same executor path.
void make_unreadable_block_files(node::configuration const& cfg) {
    {
        node::executor initialiser(cfg, false);
        REQUIRE(initialiser.do_initchain("executor lifecycle control"));
    }

    // Four bytes that are neither this network's magic nor zeroes. The scan stops
    // on them and says where, and a chain whose block data is unaccounted for is
    // one the blockchain refuses to start on.
    auto const blocks = cfg.database.directory / "blocks";
    std::error_code ec;
    std::filesystem::create_directories(blocks, ec);
    std::ofstream bad(blocks / "blk00000.dat", std::ios::binary | std::ios::trunc);
    char const garbage[] = {
        static_cast<char>(0xde), static_cast<char>(0xad),
        static_cast<char>(0xbe), static_cast<char>(0xef)};
    bad.write(garbage, sizeof(garbage));
    // Closed before it is asked. A write this small lands in the buffer, so
    // good() before the flush reports that nothing has gone wrong YET, which is
    // not the same as the bytes being on disk — and this fixture is worthless if
    // they are not.
    bad.close();
    REQUIRE(bad.good());
}

// A database the node can actually start on: the healthy path, which has to keep
// behaving after all of the above.
void make_startable_database(node::configuration const& cfg) {
    node::executor initialiser(cfg, false);
    REQUIRE(initialiser.do_initchain("executor lifecycle control"));
}

// A database directory that cannot be created at all: its parent is a file.
// Portable — no platform makes a directory under a regular file — and it fails
// in start_async() BEFORE the io thread exists, which is the one shape of start
// failure that never reaches the coroutine.
void make_uncreatable_directory(std::filesystem::path const& base) {
    std::ofstream blocker(base / "blocked");
    blocker.put('x');
    blocker.close();
    REQUIRE(blocker.good());
}

node::configuration config_under_a_file(std::filesystem::path const& base) {
    auto cfg = make_config(base);
    cfg.database.directory = base / "blocked" / "chain";
    return cfg;
}

} // namespace

// =============================================================================
// Children
// =============================================================================
//
// Hidden — the leading '.' in the tag keeps them out of a default run — and named
// without commas, because a comma in a Catch2 test spec separates two specs and
// neither half would match anything.

TEST_CASE("executor child: destroyed after a database that cannot open", "[.executor-child]") {
    watchdog guard(teardown_limit, "destroy after unopenable database");

    auto const base = claimed_dir("kth_exec_nodb");
    auto const cfg = make_config(base);
    make_unopenable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() != error::success);
    }   // <-- the destructor under test

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: destroyed after block files that cannot be read", "[.executor-child]") {
    watchdog guard(teardown_limit, "destroy after unreadable block files");

    auto const base = claimed_dir("kth_exec_badblk");
    auto const cfg = make_config(base);
    make_unreadable_block_files(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() != error::success);
    }   // <-- the destructor under test

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: destroyed without ever being started", "[.executor-child]") {
    watchdog guard(teardown_limit, "destroy without start");

    auto const base = claimed_dir("kth_exec_nostart");
    {
        node::executor host(make_config(base), false);
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: stopped after a start that failed and then destroyed", "[.executor-child]") {
    watchdog guard(teardown_limit, "stop then destroy after failed start");

    auto const base = claimed_dir("kth_exec_stopfail");
    auto const cfg = make_config(base);
    make_unopenable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() != error::success);

        // The thread is given back here rather than at destruction. Nothing can
        // observe that directly — io_thread_ is private, and has to be, or this
        // would be reading an implementation instead of a behaviour — but a stop
        // that gave nothing back would leave a joinable thread for the destructor,
        // and the process would not reach announce_completion() below at all.
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: stopped twice after a start that failed", "[.executor-child]") {
    watchdog guard(teardown_limit, "double stop after failed start");

    auto const base = claimed_dir("kth_exec_twostop");
    auto const cfg = make_config(base);
    make_unopenable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() != error::success);

        host.stop();
        host.stop();   // idempotent: the second finds nothing left to give back
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: stopped without ever being started", "[.executor-child]") {
    watchdog guard(teardown_limit, "stop without start");

    auto const base = claimed_dir("kth_exec_stopnostart");
    {
        node::executor host(make_config(base), false);
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: started and stopped and destroyed", "[.executor-child]") {
    watchdog guard(full_start_limit, "healthy start, stop and destroy");

    auto const base = claimed_dir("kth_exec_healthy");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == error::success);
        CHECK(host.running());

        host.stop();
        CHECK(host.stopped());
        CHECK_FALSE(host.running());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// The three sequences the lifecycle rule exists for
// -----------------------------------------------------------------------------

TEST_CASE("executor child: stopped while a start is still in flight", "[.executor-child]") {
    watchdog guard(full_start_limit, "stop during an in-flight start");

    // The rule: release waits for the start to publish an outcome before it takes
    // anything from it. So whatever the interleaving, by the time stop() returns
    // the start handler has run — there is no window in which the node is
    // released while `co_await node_->start()` still holds it.
    //
    // Repeated, and self-checking about whether it repeated for nothing: an
    // iteration in which the start had already finished before stop() was called
    // proves nothing, so the case requires that at least one did not.
    // Ten, not more. Every round has caught the start in flight in every run of
    // this so far, and each one stands a node up — under coverage or a sanitizer
    // that is minutes of CI for repetition that is already paying nothing. The
    // REQUIRE below is what keeps the number honest: if rounds stop landing in
    // flight, the case fails rather than passing on fewer.
    constexpr int rounds = 10;
    int caught_in_flight = 0;

    auto const base = claimed_dir("kth_exec_inflight");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    for (int round = 0; round != rounds; ++round) {
        {
            node::executor host(cfg, false);
            std::atomic<bool> outcome_published{false};

            host.start_async([&outcome_published](kth::code) {
                outcome_published.store(true);
            });

            // Read BEFORE stopping: this is what says whether this round was one
            // of the interesting ones.
            bool const was_in_flight = ! outcome_published.load();

            host.stop();

            // The invariant, on every round:
            CHECK(outcome_published.load());
            CHECK(host.stopped());

            if (was_in_flight) {
                ++caught_in_flight;
            }
        }
    }

    INFO("rounds that caught the start in flight: " << caught_in_flight << " of " << rounds);
    REQUIRE(caught_in_flight > 0);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: released while a stop request is being issued", "[.executor-child]") {
    watchdog guard(full_start_limit, "release racing stop_async");

    // stop_async() publishes `stopping` and then goes on to read node_ and to
    // install the wind-down thread. A release that saw only the state would walk
    // into the middle of that and reset node_ underneath it. Both take the
    // lifecycle lock for their decision, so one of them finds the other's mark.
    //
    // The interleaving cannot be forced from outside, so it is hunted: two
    // threads, one issuing the stop and one releasing, over many rounds. Under
    // TSan this is also where an unsynchronised access would be reported.
    // Ten. The interleaving is not forced, so this is a hunt, and what actually
    // catches it is TSan rather than the count of rounds — it reported the
    // unsynchronised node_->join() on the first one when the lock was removed.
    constexpr int rounds = 10;

    auto const base = claimed_dir("kth_exec_stoprace");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    for (int round = 0; round != rounds; ++round) {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::thread asker([&host]() { host.stop_async({}); });
        host.stop();
        asker.join();

        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: destroyed from its own stop callback", "[.executor-child]") {
    // Deliberately illegal, and the point is that it says so. The stop handler
    // runs as the last statement of the wind-down thread; destroying the executor
    // there asks that thread to join itself. Detaching would let these members go
    // under the handler still on them, and carrying on would leave a joinable
    // thread for ~std::thread to abort on with nothing said. So it terminates,
    // and names which thread it was.
    //
    // No completion marker: this child is not meant to reach one. The parent
    // requires the message and an abnormal end.
    //
    // A watchdog even so. The sleep below bounds the path where `delete host`
    // RETURNS; it bounds nothing where `delete host` blocks — a join that
    // deadlocks instead of terminating would leave the parent inside
    // std::system() until CI gave up. Its exit code fails exited_cleanly() the
    // same way, so the parent's assertions still say what happened (CR).
    watchdog guard(full_start_limit, "destroy from the stop callback");

    auto const base = claimed_dir("kth_exec_selfjoin");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    auto* host = new node::executor(cfg, true);
    REQUIRE(host->start() == kth::error::success);

    host->stop_async([host]() {
        delete host;            // <-- from the wind-down thread
    });

    // The delete above is expected to end the process before this returns.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    FAIL("destroying from the stop callback was not detected");
}

TEST_CASE("executor child: a start that fails before the io thread exists", "[.executor-child]") {
    watchdog guard(teardown_limit, "start that fails before the io thread");

    // The other end of the same rule. These two failures — the CSPRNG probe and
    // the directory — return without ever reaching the coroutine, so nothing
    // else can publish an outcome for them, and release() waits for one. Before
    // #673 start() did not return here at all.
    auto const base = claimed_dir("kth_exec_nodir");
    make_uncreatable_directory(base);

    {
        node::executor host(config_under_a_file(base), false);
        REQUIRE(host.start() != kth::error::success);
        CHECK_FALSE(host.running());

        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// Answers are owed, and owed once
// -----------------------------------------------------------------------------

TEST_CASE("executor child: the node cannot be constructed", "[.executor-child]") {
    watchdog guard(teardown_limit, "node construction throws");

    // The one step inside the guarded region that can be made to throw without
    // exhausting a machine's threads or its memory. What it stands for is the
    // whole region: the log setup, this, start_io_thread() and co_spawn are one
    // lexical scope, and the guard covers all of it.
    auto const base = claimed_dir("kth_exec_ctorthrow");
    auto const cfg = make_config(base);

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [](node::configuration const&) -> node::full_node::ptr {
                throw std::runtime_error("the node could not be constructed");
            });

        // The start answered rather than leaving a debt: without the guard this
        // call does not return at all, because nothing publishes an outcome and
        // the wait below has nothing to end it.
        REQUIRE(host.start() != kth::error::success);
        CHECK_FALSE(host.running());

        // And the object is still stoppable and destructible: a release that
        // waited on the same missing outcome would hang here instead.
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a second start while the first is still running", "[.executor-child]") {
    watchdog guard(full_start_limit, "second start during the first");

    auto const base = claimed_dir("kth_exec_twostart");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);

        std::atomic<int> refusals{0};
        host.start_async([&refusals](kth::code ec) {
            if (ec != kth::error::success) {
                ++refusals;
            }
        });

        // Refused, and its refusal is its own: it must not land in the slot the
        // admitted start still owes, or a release waiting there wakes early and
        // resets the node while the coroutine is still inside node_->start().
        auto const second = host.start();
        CHECK(second != kth::error::success);

        host.stop();

        // The admitted start's own answer survived the refusal.
        CHECK(refusals.load() == 0);
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a second start after a stop", "[.executor-child]") {
    watchdog guard(full_start_limit, "second start after stop");

    auto const base = claimed_dir("kth_exec_restart");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);
        host.stop();
        CHECK(host.stopped());

        // Restart is not offered, and `stopped` is exactly the value that used to
        // admit one — on top of a thread the previous cycle still owned.
        CHECK(host.start() != kth::error::success);
        CHECK_FALSE(host.running());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a refused start handler stops and starts again", "[.executor-child]") {
    watchdog guard(full_start_limit, "reentrant refused handler");

    // The refused handler is caller code, and it runs outside every lock. If it
    // did not, either call below would deadlock against the lock its own
    // refusal was still holding.
    auto const base = claimed_dir("kth_exec_reentrant");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::atomic<bool> reentered{false};
        host.start_async([&host, &reentered](kth::code) {
            host.stop();                                  // no deadlock
            CHECK(host.start() != kth::error::success);   // still refused
            reentered.store(true);
        });

        CHECK(reentered.load());
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: the stop callback tries to start again", "[.executor-child]") {
    watchdog guard(full_start_limit, "restart from the stop callback");

    auto const base = claimed_dir("kth_exec_cbrestart");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::atomic<bool> ran{false};
        std::atomic<bool> refused{false};
        host.stop_async([&host, &ran, &refused]() {
            // A stop callback cannot re-enable a start, and cannot deadlock
            // asking: admission is monotonic and the callback holds no lock.
            refused.store(host.start() != kth::error::success);
            ran.store(true);
        });

        host.stop();
        CHECK(ran.load());
        CHECK(refused.load());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: two callers stop at once", "[.executor-child]") {
    watchdog guard(full_start_limit, "concurrent stop waiters");

    // A teardown is claimed once. The caller that does not claim it waits for the
    // one that did and is told its result — it is neither turned away into a
    // half-torn-down object nor left on the condition variable. The watchdog is
    // what would catch the second: before the completion guard, a teardown that
    // ended any way but the happy one left `release_done_` false forever.
    auto const base = claimed_dir("kth_exec_twostop_race");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::atomic<int> returned{0};
        std::thread other([&host, &returned]() {
            host.stop();
            ++returned;
        });
        host.stop();
        ++returned;
        other.join();

        CHECK(returned.load() == 2);
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// The handoff: resources are taken after the start settles, not before
// -----------------------------------------------------------------------------

TEST_CASE("executor child: released while the node has not been built yet", "[.executor-child]") {
    watchdog guard(full_start_limit, "release before the node exists");

    // Deterministic, with no sleeps and no polling for time. Two handshakes pin
    // the order completely:
    //
    //   1. the factory runs on the starting thread, between the start being
    //      admitted and node_ being assigned. It announces that it is there and
    //      then refuses to return;
    //   2. the main thread waits for that announcement and only then stops. The
    //      factory waits for the stop to have been CLAIMED before it returns, and
    //      it can see that because a start refused by a teardown is refused with
    //      `service_stopped` where one refused by an earlier start is refused
    //      with `operation_failed`.
    //
    // So the teardown is guaranteed to be inside its claim while node_ is still
    // null. Taking node_ at claim time takes that null: the stop is skipped, and
    // the wait for run() to end never ends because nobody asked the node to stop.
    auto const base = claimed_dir("kth_exec_handoff");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    node::executor* host = nullptr;
    std::atomic<bool> in_factory{false};

    node::executor owner(cfg, false);
    host = &owner;
    node::detail::executor_test_seam::set_node_factory(owner,
        [&host, &in_factory](node::configuration const& c) {
            in_factory.store(true);
            // Hold the start here until a teardown owns the object.
            while (host->start() != kth::error::service_stopped) {
                std::this_thread::yield();
            }
            return std::make_shared<node::full_node>(c);
        });

    std::thread starter([&owner]() { owner.start_async(nullptr); });

    while ( ! in_factory.load()) {
        std::this_thread::yield();
    }

    // The node does not exist yet, and this teardown must still end.
    owner.stop();
    starter.join();

    CHECK(owner.stopped());

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: the factory hands back nothing", "[.executor-child]") {
    watchdog guard(teardown_limit, "factory returns null");

    // A factory may fail; it may not hand back nothing and let the next line find
    // out. Before this, set_fatal_handler() dereferenced the null before the
    // guard could publish anything at all.
    auto const base = claimed_dir("kth_exec_nullnode");
    auto const cfg = make_config(base);

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [](node::configuration const&) { return node::full_node::ptr{}; });
        REQUIRE(host.start() != kth::error::success);
        CHECK_FALSE(host.running());
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: stopped is published without closing", "[.executor-child]") {
    watchdog guard(full_start_limit, "stopped without close");

    // The contract a caller polls. kth_node_stopped() is this, and the loop the C
    // API documents is signal_stop, poll stopped(), then close — so a `stopped`
    // that only a close could publish is a wait that never ends.
    //
    // Deterministic: the stop handler runs as the wind-down's last statement,
    // after the value is published, so waiting for the handler is waiting for the
    // publication. No polling with a bound anywhere.
    auto const base = claimed_dir("kth_exec_stopped_contract");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);
        CHECK(host.running());

        std::promise<void> wound_down;
        auto done = wound_down.get_future();
        host.stop_async([&wound_down]() { wound_down.set_value(); });
        done.wait();

        // Nothing has been closed, and the service reports that it ended.
        CHECK(host.stopped());
        CHECK_FALSE(host.running());

        // And closing afterwards is still what gives the resources back.
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: the node is asked for outside a run", "[.executor-child]") {
    watchdog guard(full_start_limit, "node() outside a run");

    // A share, not a reference. A reference into node_ is kept alive by nothing:
    // a teardown on another thread resets that member and the reference refers to
    // a destroyed node — and a lock around the return would not change that,
    // because the reference outlives the lock by definition.
    //
    // Deterministic, and no watchdog is doing the work here: both calls return.
    auto const base = claimed_dir("kth_exec_nodeaccess");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    // The node's ACTUAL destruction, watched from its deleter. A weak_ptr taken
    // from what the accessor returns would follow that share, not the node — and
    // an accessor that hands back something non-owning satisfies such a check
    // while the node underneath is already gone. This does not.
    std::atomic<bool> destroyed{false};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&destroyed](node::configuration const& c) {
                return node::full_node::ptr(new node::full_node(c),
                    [&destroyed](node::full_node* p) {
                        destroyed.store(true);
                        delete p;
                    });
            });

        // Before a start there is no node. This used to dereference the null,
        // and both C API accessors did it without asking.
        CHECK(host.node() == nullptr);

        REQUIRE(host.start() == kth::error::success);
        {
            auto const running = host.node();
            REQUIRE(running != nullptr);

            // The teardown releases the executor's own reference...
            host.stop();
            CHECK(host.node() == nullptr);

            // ...and this share is what keeps the node alive across it. A
            // reference keeps nothing alive, and a lock around the return could
            // not change that, because the reference outlives the lock by
            // definition.
            REQUIRE_FALSE(destroyed.load());
        }

        // And letting the last share go is what ends it.
        CHECK(destroyed.load());
    }
    REQUIRE(destroyed.load());

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A stop handler that throws, on both threads that can run one
// -----------------------------------------------------------------------------

TEST_CASE("executor child: a stop handler throws on the wind-down thread", "[.executor-child]") {
    watchdog guard(full_start_limit, "throwing stop handler, wind-down thread");

    // A stop request that is handed off runs its handler as the last statement of
    // the wind-down thread. An exception out of it there leaves the thread
    // function, and an exception leaving a thread function terminates the
    // process — with nothing said about a handler being the reason.
    auto const base = claimed_dir("kth_exec_throwstop_wd");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::promise<void> reached;
        auto ran = reached.get_future();
        host.stop_async([&reached]() {
            reached.set_value();
            throw std::runtime_error("from a stop handler on the wind-down thread");
        });
        ran.wait();

        // The process is still here, and the wind-down still finished its work.
        CHECK(host.stopped());
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a stop handler throws on the caller thread", "[.executor-child]") {
    watchdog guard(full_start_limit, "throwing stop handler, caller thread");

    // The other path: a stop request that is refused calls the handler on the
    // caller's own thread and returns. That call is what kth_node_signal_stop()
    // makes, and it is `extern "C"` — an exception crossing it is undefined.
    auto const base = claimed_dir("kth_exec_throwstop_caller");
    auto const cfg = make_config(base);

    {
        // Never started, so the request is refused and the handler runs here.
        node::executor host(cfg, false);

        std::atomic<bool> ran{false};
        CHECK_NOTHROW(host.stop_async([&ran]() {
            ran.store(true);
            throw std::runtime_error("from a stop handler on the caller thread");
        }));
        CHECK(ran.load());

        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A start handler that throws, on both outcomes
// -----------------------------------------------------------------------------

TEST_CASE("executor child: a start handler throws when the start failed", "[.executor-child]") {
    watchdog guard(teardown_limit, "throwing start handler, failed start");

    // The handler is caller code on the io thread, and an exception out of it
    // there does not just vanish: co_spawn turns it into the exception_ptr the
    // completion handler reads, which then calls this same handler AGAIN with
    // `operation_failed`. An admitted start owes one answer, not one per
    // exception on the way out.
    auto const base = claimed_dir("kth_exec_throwstart_fail");
    auto const cfg = make_config(base);

    std::atomic<int> handler_runs{0};
    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [](node::configuration const&) { return node::full_node::ptr{}; });

        std::promise<void> answered;
        auto done = answered.get_future();
        host.start_async([&handler_runs, &answered](kth::code) {
            ++handler_runs;
            answered.set_value();
            throw std::runtime_error("from a start handler on the failure path");
        });
        done.wait();

        host.stop();
        CHECK(host.stopped());
    }

    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a start handler throws when the start succeeded", "[.executor-child]") {
    watchdog guard(full_start_limit, "throwing start handler, successful start");

    // The sharper one: the outcome recorded is `success`, and everything after
    // the handler in the coroutine can throw. Calling the handler a second time
    // here hands the same caller `operation_failed` for a start that succeeded.
    auto const base = claimed_dir("kth_exec_throwstart_ok");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> handler_runs{0};
    {
        node::executor host(cfg, false);

        std::promise<void> answered;
        auto done = answered.get_future();
        host.start_async([&handler_runs, &answered](kth::code ec) {
            ++handler_runs;
            CHECK(ec == kth::error::success);
            answered.set_value();
            throw std::runtime_error("from a start handler on the success path");
        });
        done.wait();

        host.stop();
        CHECK(host.stopped());
    }

    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A teardown that fails tells its waiters last
// -----------------------------------------------------------------------------

TEST_CASE("executor child: a teardown that throws releases its waiters last", "[.executor-child]") {
    watchdog guard(full_start_limit, "teardown throws, waiter waits");

    // The owner's teardown throws. What must NOT happen is that a waiter is told
    // the attempt ended while the owning call still has its catch and its whole
    // best-effort cleanup ahead of it — the waiter returns, its caller destroys
    // the executor, and the owner goes on touching work_guard_, io_context_ and
    // both threads.
    //
    // The waiter here destroys the executor the moment it returns, which is what
    // a caller of a blocking stop() is entitled to do. Under the defect that is a
    // use-after-free of the executor, reported by ASan; under the fix the waiter
    // cannot return until the owner has finished with every member.
    auto const base = claimed_dir("kth_exec_faildown");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    auto* host = new node::executor(cfg, false);
    REQUIRE(host->start() == kth::error::success);

    std::atomic<bool> owner_entered{false};
    node::detail::executor_test_seam::set_lifecycle_probe(*host,
        [&owner_entered](probe_point point) {
            if (point != probe_point::after_node_cleanup) { return; }
            owner_entered.store(true);
            throw std::runtime_error("a teardown that could not finish");
        });

    std::thread owner([host]() { host->stop(); });

    // The waiter arrives once the owner is inside its teardown, so it is a waiter
    // and not the claimer. No sleep: the fault says when.
    while ( ! owner_entered.load()) {
        std::this_thread::yield();
    }

    std::thread waiter([host]() {
        host->stop();       // blocks until the owner has published
        delete host;        // ...and the owner must be done with every member
    });

    owner.join();
    waiter.join();

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A stop asked for while the start is still in flight
// -----------------------------------------------------------------------------

// Holds a start inside the factory until the test lets it through, so a stop can
// be asked for while the start is genuinely in flight. Deterministic: the factory
// does not return until it has been told to.
struct held_start {
    std::atomic<bool> entered{false};
    std::promise<void> gate;
    std::future<void> open{gate.get_future()};

    void hold() {
        entered.store(true);
        open.wait();
    }

    void wait_until_started() {
        while ( ! entered.load()) {
            std::this_thread::yield();
        }
    }

    void let_through() {
        gate.set_value();
    }
};

TEST_CASE("executor child: a stop asked for while the start is in flight", "[.executor-child]") {
    watchdog guard(full_start_limit, "stop_async during a start that succeeds");

    auto const base = claimed_dir("kth_exec_stopstarting");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    held_start held;
    std::atomic<int> handler_runs{0};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&held](node::configuration const& c) {
                held.hold();
                return std::make_shared<node::full_node>(c);
            });

        std::thread starter([&host]() { host.start_async(nullptr); });
        held.wait_until_started();

        // Asked for while the start is in flight. This used to be answered on the
        // spot and do nothing at all: the handler ran, the caller was told the
        // node had stopped, and the start went on to publish `running`.
        std::promise<void> stopped;
        auto done = stopped.get_future();
        host.stop_async([&handler_runs, &stopped]() {
            ++handler_runs;
            stopped.set_value();
        });

        held.let_through();
        starter.join();
        done.wait();

        // The node did not end up running, and the handler ran once.
        CHECK_FALSE(host.running());
        CHECK(host.stopped());
        CHECK(handler_runs.load() == 1);

        host.stop();
        CHECK(host.stopped());
    }

    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a stop in flight when the start fails", "[.executor-child]") {
    watchdog guard(full_start_limit, "stop_async during a start that fails");

    auto const base = claimed_dir("kth_exec_stopstarting_fail");
    auto const cfg = make_config(base);

    held_start held;
    std::atomic<int> handler_runs{0};

    {
        // The factory hands back nothing, so the start fails after the request
        // has already been made. The handler still completes, exactly once.
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&held](node::configuration const&) {
                held.hold();
                return node::full_node::ptr{};
            });

        std::thread starter([&host]() { host.start_async(nullptr); });
        held.wait_until_started();

        std::promise<void> stopped;
        auto done = stopped.get_future();
        host.stop_async([&handler_runs, &stopped]() {
            ++handler_runs;
            stopped.set_value();
        });

        held.let_through();
        starter.join();
        done.wait();

        CHECK(handler_runs.load() == 1);
        CHECK_FALSE(host.running());

        host.stop();
        CHECK(host.stopped());
    }

    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a stop in flight racing a release", "[.executor-child]") {
    watchdog guard(full_start_limit, "stop_async during a start racing stop()");

    // One teardown, one wind-down thread, one handler run. The wind-down that
    // stop_async() installs and the teardown that stop() claims are decided under
    // the same lock, so neither overwrites the other's members.
    auto const base = claimed_dir("kth_exec_stopstarting_race");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    held_start held;
    std::atomic<int> handler_runs{0};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&held](node::configuration const& c) {
                held.hold();
                return std::make_shared<node::full_node>(c);
            });

        std::thread starter([&host]() { host.start_async(nullptr); });
        held.wait_until_started();

        host.stop_async([&handler_runs]() { ++handler_runs; });
        std::thread releaser([&host]() { host.stop(); });

        held.let_through();
        starter.join();
        releaser.join();
        host.stop();

        CHECK(host.stopped());
        CHECK(handler_runs.load() == 1);
    }

    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: the node's destructor re-enters the lifecycle", "[.executor-child]") {
    watchdog guard(full_start_limit, "node destructor re-enters");

    // ~full_node is unbounded node work, and node work does not run under
    // lifecycle_mutex_. Made observable rather than asserted: the node's deleter
    // asks this object a question that takes that lock. Under a teardown that
    // resets the member in place, the last share dies inside the lock and this
    // deadlocks; the watchdog is what would report it.
    auto const base = claimed_dir("kth_exec_dtor_reenter");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<bool> deleter_ran{false};
    std::atomic<bool> reentered{false};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&host, &deleter_ran, &reentered](node::configuration const& c) {
                return node::full_node::ptr(new node::full_node(c),
                    [&host, &deleter_ran, &reentered](node::full_node* p) {
                        deleter_ran.store(true);
                        // Takes lifecycle_mutex_. Reached from inside it, this
                        // never returns.
                        (void)host.node();
                        reentered.store(true);
                        delete p;
                    });
            });

        REQUIRE(host.start() == kth::error::success);
        host.stop();
        CHECK(host.stopped());
    }

    CHECK(deleter_ran.load());
    CHECK(reentered.load());

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A refused start whose handler throws
// -----------------------------------------------------------------------------
//
// The refused path called the handler directly, past invoke_start_handler(), so
// the one guarantee every other call site had did not hold here. It is caller
// code on the caller's own thread, and start_async() is what kth_node_init_run()
// calls: an exception out of it crosses `extern "C"`.

TEST_CASE("executor child: a start refused as already started throws from its handler", "[.executor-child]") {
    watchdog guard(full_start_limit, "throwing handler, refused as already started");

    auto const base = claimed_dir("kth_exec_refuse_started");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> refused_runs{0};
    std::atomic<int> admitted_runs{0};

    {
        node::executor host(cfg, false);

        std::promise<void> admitted_answered;
        auto admitted_done = admitted_answered.get_future();
        host.start_async([&admitted_runs, &admitted_answered](kth::code ec) {
            ++admitted_runs;
            CHECK(ec == kth::error::success);
            admitted_answered.set_value();
        });
        admitted_done.wait();

        // Refused because a start already happened. The handler throws, and the
        // exception must not leave start_async().
        CHECK_NOTHROW(host.start_async([&refused_runs](kth::code ec) {
            ++refused_runs;
            CHECK(ec == kth::error::operation_failed);
            throw std::runtime_error("from a refused start handler");
        }));

        CHECK(refused_runs.load() == 1);
        // And the admitted start's own answer is untouched by the refusal.
        CHECK(admitted_runs.load() == 1);

        host.stop();
        CHECK(host.stopped());
    }

    CHECK(refused_runs.load() == 1);
    CHECK(admitted_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a start refused because a teardown owns it throws from its handler", "[.executor-child]") {
    watchdog guard(full_start_limit, "throwing handler, refused as releasing");

    auto const base = claimed_dir("kth_exec_refuse_releasing");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> refused_runs{0};

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);
        host.stop();                      // claims the teardown, for good

        CHECK_NOTHROW(host.start_async([&refused_runs](kth::code ec) {
            ++refused_runs;
            // A teardown owning the object is a different refusal from a start
            // that already happened, and it is reported as one.
            CHECK(ec == kth::error::service_stopped);
            throw std::runtime_error("from a refused start handler");
        }));

        CHECK(refused_runs.load() == 1);
        CHECK(host.stopped());
    }

    CHECK(refused_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// One owner runs the wind-down
// -----------------------------------------------------------------------------

TEST_CASE("executor child: the node is asked to stop exactly once", "[.executor-child]") {
    watchdog guard(full_start_limit, "one node->stop()");

    // release() used to call node->stop() and THEN join a wind-down thread that
    // calls it too. p2p_node::stop() reads its flag and stores it separately
    // rather than exchanging it, so two callers both see `false` and both go on
    // to cancel the channels, stop_all() the manager and save() the peer
    // database — concurrently, over the same file.
    //
    // Counted rather than inferred: nothing p2p_node::stop() does says how many
    // times it ran, because it logs only when it fails.
    auto const base = claimed_dir("kth_exec_one_stop");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> stop_calls{0};
    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&stop_calls](probe_point point) {
                if (point == probe_point::before_node_stop) { ++stop_calls; }
            });
        REQUIRE(host.start() == kth::error::success);

        // A stop request hands the wind-down off; the release that follows must
        // join it and do none of its work again.
        std::promise<void> wound_down;
        auto done = wound_down.get_future();
        host.stop_async([&wound_down]() { wound_down.set_value(); });
        done.wait();

        host.stop();
        CHECK(host.stopped());

        REQUIRE(stop_calls.load() == 1u);
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a release with no wind-down still asks the node to stop", "[.executor-child]") {
    watchdog guard(full_start_limit, "release owns the stop");

    // The other half of the same property: when nobody handed a wind-down off,
    // the release is the owner and must do the stop itself. A control that only
    // counted "not twice" would pass on an executor that never stopped anything.
    auto const base = claimed_dir("kth_exec_release_stop");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> stop_calls{0};
    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&stop_calls](probe_point point) {
                if (point == probe_point::before_node_stop) { ++stop_calls; }
            });
        REQUIRE(host.start() == kth::error::success);

        host.stop();
        CHECK(host.stopped());

        REQUIRE(stop_calls.load() == 1u);
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
// A wind-down that does not finish, and a thread that does not start
// -----------------------------------------------------------------------------

TEST_CASE("executor child: a wind-down that fails is finished by the release", "[.executor-child]") {
    watchdog guard(full_start_limit, "failed wind-down completed by release");

    // The wind-down thread catches its exception and returns. What it must NOT
    // leave behind is a release that takes its existence for completion: that
    // release would skip the stop and the join and go on to destroy a full_node
    // nobody ever joined.
    auto const base = claimed_dir("kth_exec_windfail");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> stop_calls{0};
    std::atomic<int> handler_runs{0};
    std::atomic<bool> destroyed{false};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [&destroyed](node::configuration const& c) {
                return node::full_node::ptr(new node::full_node(c),
                    [&destroyed](node::full_node* p) { destroyed.store(true); delete p; });
            });
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&stop_calls](probe_point point) {
                if (point == probe_point::before_node_stop) { ++stop_calls; }
                if (point == probe_point::before_wind_down) {
                    throw std::runtime_error("a wind-down that could not finish");
                }
            });

        REQUIRE(host.start() == kth::error::success);

        host.stop_async([&handler_runs]() { ++handler_runs; });

        // The wind-down failed before doing anything, so it stopped nothing...
        std::this_thread::yield();

        host.stop();

        // ...and the release completed what was left.
        CHECK(stop_calls.load() >= 1u);
        CHECK(destroyed.load());

        // The handler never ran: it means "fully stopped", and that wind-down
        // did not stop anything.
        CHECK(handler_runs.load() == 0);
    }

    CHECK(handler_runs.load() == 0);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a failure after the wind-down finished is not a failed wind-down",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "post-cleanup failure not redone");

    // The border between "the cleanup did not happen" and "the cleanup happened
    // and then something went wrong on the way out". Only the first may be
    // redone. In production what throws here is a log; the probe stands in for
    // it, at the same place: after the node was stopped, waited on and joined.
    //
    // Under an ordering that records completion later than it happens, the
    // release finds `failed`, believes it owns the cleanup, and stops and joins
    // a node that is already stopped and joined.
    auto const base = claimed_dir("kth_exec_postclean");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> stop_calls{0};
    std::atomic<bool> threw_after{false};
    std::atomic<int> handler_runs{0};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&stop_calls, &threw_after](probe_point point) {
                if (point == probe_point::before_node_stop) { ++stop_calls; }
                if (point == probe_point::after_wind_down) {
                    threw_after.store(true);
                    throw std::runtime_error("a failure after the cleanup was done");
                }
            });

        REQUIRE(host.start() == kth::error::success);

        std::promise<void> handled;
        auto done = handled.get_future();
        host.stop_async([&handler_runs, &handled]() {
            ++handler_runs;
            handled.set_value();
        });

        // The handler still runs: the wind-down DID finish, and a failure after
        // that does not make it unfinished. Checked, not required, so that a
        // build where it does NOT run still reaches the second half below: the
        // defect has two consequences and the control shows both.
        auto const handled_in_time =
            done.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
        CHECK(handled_in_time);
        CHECK(handler_runs.load() == 1);

        // Not the point of the control, but the fixture for it: what follows
        // measures nothing unless the failure really happened, after one stop.
        REQUIRE(threw_after.load());
        REQUIRE(stop_calls.load() == 1u);

        // The wind-down thread is done either way — it threw and returned — so
        // the release below is not racing it, whichever way it classified itself.

        // The release joins that thread, reads `completed`, and does nothing
        // again. A second stop here is the defect this control exists for.
        host.stop();
        CHECK(host.stopped());
        CHECK(stop_calls.load() == 1u);
    }

    // ...including through the destructor.
    CHECK(stop_calls.load() == 1u);
    CHECK(handler_runs.load() == 1);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: a wind-down thread that cannot be started", "[.executor-child]") {
    watchdog guard(full_start_limit, "wind-down thread fails to start");

    // std::thread can throw, and stop_async() is what kth_node_signal_stop()
    // calls across `extern "C"`. The exception must not cross it — and the stop
    // must not be reported as having happened, because the node is still running.
    auto const base = claimed_dir("kth_exec_nothread");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<int> handler_runs{0};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [](probe_point point) {
                if (point == probe_point::before_wind_down_thread) {
                    throw std::system_error(std::make_error_code(
                        std::errc::resource_unavailable_try_again),
                        "no thread for you");
                }
            });

        REQUIRE(host.start() == kth::error::success);
        // The fixture, made explicit: if the node is not running when the stop is
        // asked for, the request is refused for a reason that has nothing to do
        // with the thread, and what follows would be measuring the wrong thing.
        REQUIRE(host.running());

        CHECK_NOTHROW(host.stop_async([&handler_runs]() { ++handler_runs; }));

        // Nothing was stopped, so nothing says it was.
        CHECK(handler_runs.load() == 0);
        CHECK_FALSE(host.stopped());
        CHECK(host.running());

        // And the object is still one a caller can wind down.
        node::detail::executor_test_seam::set_lifecycle_probe(host, [](probe_point) {});
        host.stop();
        CHECK(host.stopped());
    }

    CHECK(handler_runs.load() == 0);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

// =============================================================================
// Parents
// =============================================================================

TEST_CASE("a start that fails on the database is destroyed without aborting", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: destroyed after a database that cannot open"));
}

TEST_CASE("a start that fails on the block scan is destroyed without aborting", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: destroyed after block files that cannot be read"));
}

TEST_CASE("an executor that never started is destroyed without aborting", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: destroyed without ever being started"));
}

TEST_CASE("stop after a failed start leaves nothing for the destructor to abort on", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopped after a start that failed and then destroyed"));
}

TEST_CASE("stopping twice after a failed start is harmless", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopped twice after a start that failed"));
}

TEST_CASE("stopping an executor that never started is harmless", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopped without ever being started"));
}

TEST_CASE("a node that starts still stops and is destroyed cleanly", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: started and stopped and destroyed"));
}

// -----------------------------------------------------------------------------
// The controls that prove the others discriminate
// -----------------------------------------------------------------------------
//
// Everything above asserts an absence, and an absence is cheap to satisfy by
// accident. These two ask the harness for the outcomes it must be able to tell
// apart, so a run in which the children quietly did nothing cannot look like a
// run in which they all passed.

TEST_CASE("executor child: fails on purpose", "[.executor-child]") {
    FAIL("this case exists to fail");
}

TEST_CASE("the subprocess harness reports a child that fails", "[executor][lifecycle]") {
    auto const run = run_child("executor child: fails on purpose");
    INFO("child " << run.describe());
    CHECK(run.out.find(child_completed) == std::string::npos);
    REQUIRE_FALSE(run.exited_cleanly());
}

TEST_CASE("the subprocess harness reports a child that ran nothing", "[executor][lifecycle]") {
    auto const run = run_child("executor child: no case has this name");
    INFO("child " << run.describe());
    CHECK(run.out.find(child_completed) == std::string::npos);
    REQUIRE_FALSE(run.exited_cleanly());
}

TEST_CASE("a stop during an in-flight start waits for the start to finish", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopped while a start is still in flight"));
}

TEST_CASE("a release racing a stop request does not walk into it", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: released while a stop request is being issued"));
}

TEST_CASE("a start that fails before the io thread exists still returns", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a start that fails before the io thread exists"));
}

TEST_CASE("destroying from the stop callback is refused by name", "[executor][lifecycle]") {
    auto const run = run_child("executor child: destroyed from its own stop callback");
    INFO("child " << run.describe());
    INFO("child stdout:\n" << run.out);
    INFO("child stderr:\n" << run.err);

    // It did not carry on,
    REQUIRE_FALSE(run.exited_cleanly());
    CHECK(run.out.find(child_completed) == std::string::npos);
    // it did not sit there until the case gave up,
    CHECK(run.out.find("destroying from the stop callback was not detected") == std::string::npos);
    // and it said which thread could not join itself rather than terminating with
    // nothing to read. Looked for in BOTH streams: the sink layout is not what
    // this case is about, and spdlog writes the level to whichever one it is
    // configured with (CR).
    auto const said_it = run.out.find("wind-down thread cannot join itself") != std::string::npos
        || run.err.find("wind-down thread cannot join itself") != std::string::npos;
    CHECK(said_it);
}

TEST_CASE("a node that cannot be constructed still answers its start", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the node cannot be constructed"));
}

TEST_CASE("a second start during the first does not answer for it", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a second start while the first is still running"));
}

TEST_CASE("a second start after a stop is refused", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a second start after a stop"));
}

TEST_CASE("a refused start handler can stop and start without deadlocking", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a refused start handler stops and starts again"));
}

TEST_CASE("a stop callback cannot re-enable a start", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the stop callback tries to start again"));
}

TEST_CASE("two callers stopping at once both come back", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: two callers stop at once"));
}

TEST_CASE("a release that arrives before the node exists still ends", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: released while the node has not been built yet"));
}

TEST_CASE("a factory that hands back nothing fails the start normally", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the factory hands back nothing"));
}

TEST_CASE("the service reports that it ended without being closed", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopped is published without closing"));
}

TEST_CASE("the node is handed over as a share that outlives the teardown", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the node is asked for outside a run"));
}

TEST_CASE("a stop handler that throws on the wind-down thread is reported", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a stop handler throws on the wind-down thread"));
}

TEST_CASE("a stop handler that throws on the caller thread does not escape", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a stop handler throws on the caller thread"));
}

TEST_CASE("a teardown that fails tells its waiters only when it is done", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a teardown that throws releases its waiters last"));
}

TEST_CASE("a stop during a start that succeeds leaves nothing running", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a stop asked for while the start is in flight"));
}

TEST_CASE("a stop during a start that fails still completes once", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a stop in flight when the start fails"));
}

TEST_CASE("a stop during a start and a release make one teardown between them", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a stop in flight racing a release"));
}

TEST_CASE("the node's destructor is not run under the lifecycle lock", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the node's destructor re-enters the lifecycle"));
}

TEST_CASE("a start handler that throws on a failed start runs once", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a start handler throws when the start failed"));
}

TEST_CASE("a start handler that throws on a successful start runs once", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a start handler throws when the start succeeded"));
}

TEST_CASE("a start refused as already started does not let its handler escape", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a start refused as already started throws from its handler"));
}

TEST_CASE("a start refused by a teardown does not let its handler escape", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a start refused because a teardown owns it throws from its handler"));
}

TEST_CASE("the node is asked to stop once when a wind-down was handed off", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: the node is asked to stop exactly once"));
}

TEST_CASE("the node is asked to stop once when the release owns it", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a release with no wind-down still asks the node to stop"));
}

TEST_CASE("a release finishes a wind-down that did not", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a wind-down that fails is finished by the release"));
}

TEST_CASE("a wind-down thread that cannot start is not a stop that happened", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: a wind-down thread that cannot be started"));
}

TEST_CASE("a failure after a finished wind-down is not redone", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: a failure after the wind-down finished is not a failed wind-down"));
}

// =============================================================================
// A stop does not wait out the heartbeat (#683)
// =============================================================================
//
// The heartbeat waits ten seconds between beats, and it used to wait on a timer
// that lived inside its own coroutine frame — reachable by nobody. Its exit
// condition was therefore re-read only when that timer expired on its own.
//
// The teardown waits for that coroutine: release() blocks on run_completed_,
// which is satisfied only after `node_->run() && heartbeat()` completes. So
// stopping a node that had nothing left to do could take ten seconds, in
// production and in every control here that stands one up and takes it down.
//
// io_context_.stop(), which would end the wait, runs after that wait.

namespace {

// Comfortably below the ten seconds, and comfortably above what the work
// actually takes, so this measures the wait rather than the machine. A CI runner
// several times slower than a development machine still lands well inside it.
constexpr auto stop_budget = std::chrono::seconds(5);

} // namespace

TEST_CASE("executor child: a stop handed off does not wait out the heartbeat either",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "handed-off stop does not wait out the heartbeat");

    // stop_async() hands the wind-down to its own thread, and that thread asks
    // the node to stop; release() does the same on the caller's thread. Both have
    // to wake the heartbeat, and this case walks the handed-off one end to end:
    // the request is accepted, the wind-down completes, the handler runs, and the
    // object is still one a caller can take down afterwards.
    auto const base = claimed_dir("kth_exec_heartbeat_async");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        std::promise<void> wound_down;
        auto done = wound_down.get_future();

        host.stop_async([&wound_down]() { wound_down.set_value(); });
        REQUIRE(done.wait_for(full_start_limit) == std::future_status::ready);

        // NO wall-clock bound here, on purpose. This window ends when the stop
        // handler runs, which is after node->join(), and that join waits on
        // p2p_node's status task — a timer with this same defect, ten seconds
        // long, and not this PR's to fix (#689). A run where the node was up
        // long enough for that task to arm takes ten seconds whether or not the
        // heartbeat was woken, so a bound here measures the other component and
        // passes or fails by platform: it passed on Linux and failed on macOS at
        // 11.39s, with nothing wrong.
        //
        // What proves the heartbeat was cut short is the probe, in
        // "a woken heartbeat ends once and counts no beat", which observes it
        // directly and does not depend on time at all.

        // And the object is still one a caller can take down.
        host.stop();
        CHECK(host.stopped());
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: stopping twice is still safe and still prompt", "[.executor-child]") {
    watchdog guard(full_start_limit, "second stop after a woken heartbeat");

    // The wake is posted, and a second stop posts another one at a moment when
    // the heartbeat has already ended and the timer will never be armed again.
    // Cancelling a timer nobody is waiting on has to be a no-op, not a fault.
    auto const base = claimed_dir("kth_exec_heartbeat_twice");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    {
        node::executor host(cfg, false);
        REQUIRE(host.start() == kth::error::success);

        host.stop();
        CHECK(host.stopped());

        auto const before = std::chrono::steady_clock::now();
        host.stop();
        auto const elapsed = std::chrono::steady_clock::now() - before;

        CHECK(host.stopped());
        CHECK(elapsed < stop_budget);
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: an object that never started leaves no timer behind",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "never started, nothing to wake");

    // No start, so no heartbeat and no armed timer — and the teardown still has
    // to be prompt rather than waiting for something that was never scheduled.
    auto const base = claimed_dir("kth_exec_heartbeat_unstarted");
    auto const cfg = make_config(base);

    {
        node::executor host(cfg, false);

        auto const before = std::chrono::steady_clock::now();
        host.stop();
        auto const elapsed = std::chrono::steady_clock::now() - before;
        CHECK(elapsed < stop_budget);
    }

    // And a failed start, which DOES create the io thread before it knows the
    // node cannot start — so it is the case where a timer could be left behind.
    // The factory hands back nothing, which is how a start fails without needing
    // a broken database.
    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_node_factory(host,
            [](node::configuration const&) { return node::full_node::ptr{}; });
        REQUIRE(host.start() != kth::error::success);

        auto const before = std::chrono::steady_clock::now();
        host.stop();
        auto const elapsed = std::chrono::steady_clock::now() - before;
        CHECK(elapsed < stop_budget);
    }

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a handed-off stop does not wait out the heartbeat", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: a stop handed off does not wait out the heartbeat either"));
}

TEST_CASE("stopping twice after a woken heartbeat is safe", "[executor][lifecycle]") {
    require_clean_child(run_child("executor child: stopping twice is still safe and still prompt"));
}

TEST_CASE("an executor that never ran leaves no heartbeat behind", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: an object that never started leaves no timer behind"));
}

TEST_CASE("executor child: a woken heartbeat ends once and counts no beat",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "heartbeat woken exactly once");

    // The three facts the wake rests on, and none of them is visible from
    // outside: the timer's handler runs ONCE, `operation_aborted` ends the loop,
    // and it is not counted as a beat. Reported through the lifecycle probe,
    // which exists for exactly this kind of fact.
    auto const base = claimed_dir("kth_exec_heartbeat_once");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> beats{0};
    std::atomic<unsigned> wakes{0};
    std::atomic<bool> armed{false};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&beats, &wakes, &armed](probe_point point) {
                if (point == probe_point::heartbeat_armed) { armed.store(true); }
                if (point == probe_point::heartbeat_beat) { ++beats; }
                if (point == probe_point::heartbeat_woken) { ++wakes; }
            });

        REQUIRE(host.start() == kth::error::success);

        // OBSERVED, not assumed. A stop that arrives before the heartbeat has
        // armed its timer ends the loop on its own condition — correct, and not
        // what this case measures. Under TSan the stop wins that race routinely,
        // which is how this control came to say `wakes == 0` on a build where
        // nothing was wrong.
        //
        // The watchdog above is what bounds this: a heartbeat that never arms is
        // a defect of its own, and hanging here is how it would show.
        while ( ! armed.load()) {
            // Slept, not spun. The heartbeat arms on the io thread, and a pure
            // yield loop here burns a core competing with it — on a single-core
            // runner that is the thread this loop is waiting for. The wait is
            // bounded by the watchdog either way.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::promise<void> wound_down;
        auto done = wound_down.get_future();
        host.stop_async([&wound_down]() { wound_down.set_value(); });
        REQUIRE(done.wait_for(full_start_limit) == std::future_status::ready);

        host.stop();
        CHECK(host.stopped());
    }

    // Ended by the wake, exactly once. A second wake — the stop above sends one
    // too — must not produce a second ending, because by then the loop is gone
    // and the timer is never armed again.
    CHECK(wakes.load() == 1u);

    // And no beat: the whole run is far shorter than one ten-second period, so a
    // beat here would mean an abort was counted as one.
    CHECK(beats.load() == 0u);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: destroying after a stop neither repeats nor abandons work",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "destructor after stop");

    // The destructor runs release() again. With the heartbeat already ended, the
    // wake it sends has no timer to cancel and no loop to end — and the object
    // still has to come down promptly rather than waiting for anything.
    auto const base = claimed_dir("kth_exec_heartbeat_dtor");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> wakes{0};
    std::chrono::steady_clock::duration destroy_time{};

    {
        auto host = std::make_unique<node::executor>(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(*host,
            [&wakes](probe_point point) {
                if (point == probe_point::heartbeat_woken) { ++wakes; }
            });

        REQUIRE(host->start() == kth::error::success);
        host->stop();
        REQUIRE(host->stopped());

        auto const before = std::chrono::steady_clock::now();
        host.reset();
        destroy_time = std::chrono::steady_clock::now() - before;
    }

    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(destroy_time);
    CAPTURE(ms.count());
    CHECK(destroy_time < stop_budget);

    // The ending happened once, on the stop; the destructor did not produce
    // another one out of a loop that was already gone.
    CHECK(wakes.load() <= 1u);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("executor child: destroying a running executor wakes the heartbeat too",
    "[.executor-child]") {
    watchdog guard(full_start_limit, "destroy while running");

    // No stop at all: the destructor is the teardown. It runs release(), which
    // asks the node to stop and wakes the heartbeat on the same path — but a
    // caller who never calls stop() is a case of its own, and nothing else here
    // covered it.
    //
    // Asserted through the probe rather than a clock. The window ends when the
    // destructor returns, which is after node->join(), and that join waits on
    // p2p_node's status task — the same defect in another component (#689), ten
    // seconds long. A bound here would measure that instead, which is how the
    // wall-clock version of this case passed on Linux and failed on macOS.
    auto const base = claimed_dir("kth_exec_heartbeat_running_dtor");
    auto const cfg = make_config(base);
    make_startable_database(cfg);

    std::atomic<unsigned> beats{0};
    std::atomic<unsigned> wakes{0};
    std::atomic<bool> armed{false};

    {
        node::executor host(cfg, false);
        node::detail::executor_test_seam::set_lifecycle_probe(host,
            [&beats, &wakes, &armed](probe_point point) {
                if (point == probe_point::heartbeat_armed) { armed.store(true); }
                if (point == probe_point::heartbeat_beat) { ++beats; }
                if (point == probe_point::heartbeat_woken) { ++wakes; }
            });

        REQUIRE(host.start() == kth::error::success);

        // Observed, so the destructor really is cutting a wait short.
        while ( ! armed.load()) {
            // Slept, not spun. The heartbeat arms on the io thread, and a pure
            // yield loop here burns a core competing with it — on a single-core
            // runner that is the thread this loop is waiting for. The wait is
            // bounded by the watchdog either way.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // The wait was ended by the destructor's own stop, once, and no abort was
    // counted as a beat.
    CHECK(wakes.load() == 1u);
    CHECK(beats.load() == 0u);

    announce_completion();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE("a woken heartbeat ends once and counts no beat", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: a woken heartbeat ends once and counts no beat"));
}

TEST_CASE("destroying after a stop is prompt and does not repeat", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: destroying after a stop neither repeats nor abandons work"));
}

TEST_CASE("destroying a running executor wakes the heartbeat", "[executor][lifecycle]") {
    require_clean_child(run_child(
        "executor child: destroying a running executor wakes the heartbeat too"));
}
