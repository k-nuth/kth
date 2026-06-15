// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_TEST_HELPERS_HPP_
#define KTH_CAPI_TEST_HELPERS_HPP_

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <catch2/catch_test_macros.hpp>

// Helper macros that locally suppress -Wunused-result around a single
// statement. Used by KTH_EXPECT_ABORT below to allow death tests to call
// KTH_OWNED functions without capturing their return value (the child
// process is about to abort, so any leak is irrelevant). Guarded so the
// pragmas are only emitted on toolchains that understand them.
#if defined(__GNUC__) || defined(__clang__)
#define KTH_TEST_PUSH_NO_UNUSED_RESULT \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunused-result\"")
#define KTH_TEST_POP_DIAG _Pragma("GCC diagnostic pop")
#else
#define KTH_TEST_PUSH_NO_UNUSED_RESULT
#define KTH_TEST_POP_DIAG
#endif

// Mark the calling process as non-dumpable so the kernel skips its
// core-dump path entirely (no core_pattern handler invoked). See the
// comment in KTH_EXPECT_ABORT below for the why.
#if defined(__linux__)
#define IF_LINUX_DISABLE_CORE_DUMP() prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)
#else
#define IF_LINUX_DISABLE_CORE_DUMP() ((void)0)
#endif

// Verify that a statement triggers KTH_PRECONDITION (abort).
// Forks a child process to run the statement; if the child does not abort,
// the test fails. Catch2-friendly: uses REQUIRE for assertions.
//
// Note: POSIX-only (uses fork/waitpid).
//
// The `stmt` argument may be a call to a KTH_OWNED function whose return
// value would normally trigger -Wunused-result. Death-test bodies are
// allowed to discard those results (the process is about to abort anyway),
// so we suppress the warning locally around the call site.
#define KTH_EXPECT_ABORT(stmt) do {                                  \
    pid_t _pid = fork();                                             \
    REQUIRE(_pid >= 0);                                              \
    if (_pid == 0) {                                                 \
        /* SIGABRT below normally triggers the kernel's core-dump    \
         * pass, which on most Linux distros pipes to                \
         * systemd-coredump regardless of RLIMIT_CORE (the limit     \
         * only suppresses the file write — the handler still runs  \
         * and adds tens of ms per abort). With ~250 death tests    \
         * per run that's ~9 s locally and hangs on GHA containers  \
         * where the piped handler isn't even present. PR_SET_      \
         * DUMPABLE=0 tells the kernel to skip the core-dump path   \
         * *entirely*: SIGABRT still gets delivered, parent's       \
         * waitpid() still sees WIFSIGNALED + WTERMSIG==SIGABRT,    \
         * but no handler runs. ~500x faster than RLIMIT_CORE=0     \
         * in microbenchmarks. Linux-only; macOS and BSDs don't     \
         * have the systemd-coredump issue. */                      \
        IF_LINUX_DISABLE_CORE_DUMP();                                \
        /* child: silence stderr to avoid abort() messages */        \
        freopen("/dev/null", "w", stderr);                           \
        /* Catch2 installs its own SIGABRT handler in the test       \
         * driver. When our child inherits it, the handler           \
         * intercepts the abort(), prints a FAILED line into the     \
         * shared stdout, and exits the child normally — so the      \
         * parent then sees WIFSIGNALED == false and the death       \
         * test misfires. Reset SIGABRT to the default handler so    \
         * abort() actually terminates the child via the signal. */  \
        signal(SIGABRT, SIG_DFL);                                    \
        KTH_TEST_PUSH_NO_UNUSED_RESULT                               \
        stmt;                                                        \
        KTH_TEST_POP_DIAG                                            \
        _exit(0);  /* did not abort */                               \
    }                                                                \
    int _status = 0;                                                 \
    REQUIRE(waitpid(_pid, &_status, 0) == _pid);                     \
    REQUIRE(WIFSIGNALED(_status));                                   \
    REQUIRE(WTERMSIG(_status) == SIGABRT);                           \
} while(0)

#endif /* KTH_CAPI_TEST_HELPERS_HPP_ */
