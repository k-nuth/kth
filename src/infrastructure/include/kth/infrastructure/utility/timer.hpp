// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TIMER_HPP
#define KTH_INFRASTRUCTURE_TIMER_HPP

#include <chrono>
#include <cstddef>
#include <ctime>
#include <string>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/utility/asio.hpp>

namespace kth {

/// Current zulu (utc) time using the wall clock.
/// BUGBUG: en.wikipedia.org/wiki/Year_2038_problem
inline std::time_t zulu_time()
{
    using wall_clock = std::chrono::system_clock;
    auto const now = wall_clock::now();
    return wall_clock::to_time_t(now);
}

/// Standard date-time string, e.g. Sun Oct 17 04:41:13 2010, locale dependent.
inline std::string local_time()
{
    static constexpr size_t size = 24;
    char buffer[size];
    auto const time = zulu_time();

    // std::strftime is required because gcc doesn't implement std::put_time.
    auto result = std::strftime(buffer, size, "%c", std::localtime(&time));

    // If count was reached before the entire string could be stored, zero is
    // returned and contents are undefined, so do not return result.
    return result == 0 ? "" : buffer;
};

// From: github.com/picanumber/bureaucrat/blob/master/time_lapse.h
// boost::timer::auto_cpu_timer requires the boost timer lib dependency.

/// Class to measure the execution time of a callable.
template <typename Time=asio::milliseconds, typename Clock=asio::steady_clock>
struct timer
{
    /// Returns the quantity (count) of the elapsed time as TimeT units.
    template <typename Function, typename ...Args>
    static typename Time::rep execution(Function func, Args&&... args) {
        auto const start = Clock::now();
        func(std::forward<Args>(args)...);
        auto const difference = Clock::now() - start;
        auto const duration = std::chrono::duration_cast<Time>(difference);
        return duration.count();
    }

    /// Returns the duration (in chrono's type system) of the elapsed time.
    template <typename Function, typename... Args>
    static Time duration(Function func, Args&&... args) {
        auto start = Clock::now();
        func(std::forward<Args>(args)...);
        return std::chrono::duration_cast<Time>(Clock::now() - start);
    }
};

} // namespace kth

#endif
