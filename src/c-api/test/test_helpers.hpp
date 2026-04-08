// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_TEST_HELPERS_HPP_
#define KTH_CAPI_TEST_HELPERS_HPP_

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
        /* child: silence stderr to avoid abort() messages */        \
        freopen("/dev/null", "w", stderr);                           \
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
