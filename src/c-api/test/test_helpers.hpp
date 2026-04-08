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

// Verify that a statement triggers KTH_PRECONDITION (abort).
// Forks a child process to run the statement; if the child does not abort,
// the test fails. Catch2-friendly: uses REQUIRE for assertions.
//
// Note: POSIX-only (uses fork/waitpid).
#define KTH_EXPECT_ABORT(stmt) do {                                  \
    pid_t _pid = fork();                                             \
    REQUIRE(_pid >= 0);                                              \
    if (_pid == 0) {                                                 \
        /* child: silence stderr to avoid abort() messages */        \
        freopen("/dev/null", "w", stderr);                           \
        stmt;                                                        \
        _exit(0);  /* did not abort */                               \
    }                                                                \
    int _status = 0;                                                 \
    REQUIRE(waitpid(_pid, &_status, 0) == _pid);                     \
    REQUIRE(WIFSIGNALED(_status));                                   \
    REQUIRE(WTERMSIG(_status) == SIGABRT);                           \
} while(0)

#endif /* KTH_CAPI_TEST_HELPERS_HPP_ */
