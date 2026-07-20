// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <stdexcept>
#include <kth/infrastructure.hpp>

//#ifdef _MSC_VER
#ifdef BOOST_WINDOWS_API
    #include <windows.h>
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/resource.h>
    #include <sys/types.h>
    #ifndef PRIO_MAX
        #define PRIO_MAX 20
    #endif
    #define THREAD_PRIORITY_ABOVE_NORMAL (-2)
    #define THREAD_PRIORITY_NORMAL 0
    #define THREAD_PRIORITY_BELOW_NORMAL 2
    #define THREAD_PRIORITY_LOWEST PRIO_MAX
#endif

using namespace kth;

// Start Test Suite: thread tests

static int get_thread_priority_test() {
#if defined(_WIN32)
    return GetThreadPriority(GetCurrentThread());
#elif defined(PRIO_THREAD)
    return getpriority(PRIO_LWP, pthread_self());
#else
    return getpriority(PRIO_PROCESS, getpid());
#endif
}

void set_thread_priority_test(int priority) {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), priority);
#elif defined(PRIO_THREAD)
    setpriority(PRIO_THREAD, pthread_self(), priority);
#else
    setpriority(PRIO_PROCESS, getpid(), priority);
#endif
}

#ifdef _MSC_VER

// WARNING: This creates a side effect that may impact other tests.
// We must run these sequentially to prevent concurrency-driven test failures.
TEST_CASE("thread set thread priorites all set as expected", "[thread tests]") {
    // Save so we can restore at the end of this test case.
    int const save = get_thread_priority_test();

    set_priority(thread_priority::high);
    REQUIRE(THREAD_PRIORITY_ABOVE_NORMAL == get_thread_priority_test());
    set_priority(thread_priority::normal);
    REQUIRE(THREAD_PRIORITY_NORMAL == get_thread_priority_test());
    set_priority(thread_priority::low);
    REQUIRE(THREAD_PRIORITY_BELOW_NORMAL == get_thread_priority_test());
    set_priority(thread_priority::lowest);
    REQUIRE(THREAD_PRIORITY_LOWEST == get_thread_priority_test());

    // Restore and verify test execution thread priority to minimize side effect.
    set_thread_priority_test(save);
    REQUIRE(save == get_thread_priority_test());
}

#else

// WARNING: This creates a side effect that may impact other tests.
// We must run these sequentially to prevent concurrency-driven test failures.
TEST_CASE("thread set thread priorites all set as expected", "[thread tests]") {
    // Save so we can restore at the end of this test case.
    int const save = get_thread_priority_test();

    // Haven't had any luck matching the set and get priority calls as in win.
    REQUIRE_NOTHROW(set_priority(thread_priority::high));
    REQUIRE_NOTHROW(set_priority(thread_priority::normal));
    REQUIRE_NOTHROW(set_priority(thread_priority::low));
    REQUIRE_NOTHROW(set_priority(thread_priority::lowest));

    // Restore and verify test execution thread priority to minimize side effect.
    set_thread_priority_test(save);
}

#endif

TEST_CASE("thread set thread priority invalid throws invalid argument", "[thread tests]") {
    REQUIRE_THROWS_AS(set_priority(static_cast<thread_priority>(42)), std::invalid_argument);
}

// End Test Suite
