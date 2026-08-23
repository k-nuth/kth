// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// TEST-ONLY. Not installed, not compiled into any library.
//
// Running a case in a subprocess, with a watchdog that turns a deadlock into a
// diagnostic exit rather than a CI timeout with no explanation.
//
// Extracted verbatim from test/executor_lifecycle.cpp, where it was written for
// the executor's teardown controls. p2p_node needs the same thing for the same
// reason: a stop that does not end a wait leaves join() blocked, and an
// in-process watchdog that hard-exits would take the whole suite with it.
//
// Everything here is `inline` or lives in an anonymous namespace, so including
// it from two translation units is not an ODR problem.

#ifndef KTH_NODE_TEST_CHILD_PROCESS_HARNESS_HPP
#define KTH_NODE_TEST_CHILD_PROCESS_HARNESS_HPP

#include <test_helpers.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <fstream>
#include <system_error>
#include <thread>

#if ! defined(_WIN32)
#include <sys/wait.h>
#endif

namespace kth::test {
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

} // namespace
} // namespace kth::test

#endif // KTH_NODE_TEST_CHILD_PROCESS_HARNESS_HPP
