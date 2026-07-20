// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/thread.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>

//#ifdef _MSC_VER
#ifdef BOOST_WINDOWS_API
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sys/resource.h>
    #include <sys/types.h>
    #include <unistd.h>
    #ifndef PRIO_MAX
        #define PRIO_MAX 20
    #endif
    #define THREAD_PRIORITY_ABOVE_NORMAL (-2)
    #define THREAD_PRIORITY_NORMAL 0
    #define THREAD_PRIORITY_BELOW_NORMAL 2
    #define THREAD_PRIORITY_LOWEST PRIO_MAX
#endif

namespace kth {

// Privately map the class enum thread priority value to an interger.
static int get_priority(thread_priority priority)
{
    switch (priority) {
        case thread_priority::high:
            return THREAD_PRIORITY_ABOVE_NORMAL;
        case thread_priority::normal:
            return THREAD_PRIORITY_NORMAL;
        case thread_priority::low:
            return THREAD_PRIORITY_BELOW_NORMAL;
        case thread_priority::lowest:
            return THREAD_PRIORITY_LOWEST;
        default:
            throw std::invalid_argument("priority");
    }
}

// Set the thread priority (or process if thread priority is not avaliable).
void set_priority(thread_priority priority)
{
    auto const prioritization = get_priority(priority);

//#if defined(_MSC_VER)
#ifdef BOOST_WINDOWS_API
    SetThreadPriority(GetCurrentThread(), prioritization);
#elif defined(PRIO_THREAD)
    setpriority(PRIO_THREAD, pthread_self(), prioritization);
#else
    setpriority(PRIO_PROCESS, getpid(), prioritization);
#endif
}

thread_priority priority(bool priority)
{
    return priority ? thread_priority::high : thread_priority::normal;
}

inline size_t cores()
{
    // Cores must be at least 1 (guards against irrational API return).
    return (std::max)(std::thread::hardware_concurrency(), 1u);
}

// This is used to default the number of threads to the number of cores and to
// ensure that no less than one thread is configured.
size_t thread_default(size_t configured)
{
    if (configured == 0) {
        return cores();
}

    // Configured but no less than 1.
    return (std::max)(configured, size_t(1));
}

// This is used to ensure that threads does not exceed cores in the case of
// parallel work distribution, while allowing the user to reduce parallelism so
// as not to monopolize the processor. It also makes optimal config easy (0).
size_t thread_ceiling(size_t configured)
{
    if (configured == 0) {
        return cores();
}

    // Cores/1 but no more than configured.
    return (std::min)(configured, cores());
}

// This is used to ensure that at least a minimum required number of threads is
// allocated, so that thread starvation does not occur. It also allows the user
// to increase threads above minimum. It always ensures at least core threads.
size_t thread_floor(size_t configured)
{
    if (configured == 0) {
        return cores();
}

    // Configured but no less than cores/1.
    return (std::max)(configured, cores());
}

} // namespace kth
