// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
#define KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

#if ! defined(KTH_BROWSER_NODE)
#include <parlay/parallel.h>
#endif

namespace kth::blockchain {

// The number of threads a fork-join over `count` independent items uses: the
// hardware's, never more than the items, and one in the browser node, whose
// Worker owns validation and creates no native threads.
inline size_t fork_join_workers(size_t count) noexcept {
#if defined(KTH_BROWSER_NODE)
    size_t const hw = 1;
#else
    size_t const hw = std::thread::hardware_concurrency();
#endif
    return std::max<size_t>(1, std::min<size_t>(hw ? hw : 1, count));
}

// Calls body(i) for every i in [0, count) and returns once every call has
// finished, so nothing outlives the call. The calls run on the scheduler the
// whole process shares (ParlayLib's, work stealing): its threads live as long
// as the process, so no thread is started or joined per call, and whichever
// thread asks is served by the same ones. The calls run concurrently, so each
// must only read shared state and write the output slot of its own index; the
// caller reads the slots after the return. Each call runs from start to end on
// one thread. Nothing is reported through ordering: a caller that wants the
// first failure scans its slots by index.
//
// A body may fork again: a call made from a thread of the scheduler runs on
// it, as the worker it is, and never waits for the scheduler it is part of.
//
// False when a call threw; the slots are then not to be trusted. The other
// calls still run.
template <typename Body>
[[nodiscard]]
bool fork_join_for(size_t count, Body const& body) {
    if (count == 0) {
        return true;
    }
    // One mark per index, written only by the call of that index and read
    // after the return: the calls share nothing here either.
    std::vector<char> threw(count, 0);
    auto const call = [&body, &threw](size_t i) noexcept {
        try {
            body(i);
        } catch (...) {
            // A thread must not let an exception escape (std::terminate).
            threw[i] = 1;
        }
    };

#if defined(KTH_BROWSER_NODE)
    for (size_t i = 0; i < count; ++i) {
        call(i);
    }
#else
    if (fork_join_workers(count) == 1) {
        for (size_t i = 0; i < count; ++i) {
            call(i);
        }
    } else {
        // One index per task: the items are whole buckets, blocks or
        // transactions, not elements cheap enough to group.
        parlay::execute_shared([count, &call] {
            parlay::parallel_for(0, count, call, 1);
        });
    }
#endif
    return std::ranges::none_of(threw, [](char t) { return t != 0; });
}

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
