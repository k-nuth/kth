// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
#define KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

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
// finished: the calling thread takes a share and the threads it starts are
// joined before returning, so nothing outlives the call. The calls run
// concurrently, so each must only read shared state and write the output slot
// of its own index; the caller reads the slots after the return. Nothing is
// reported through ordering: a caller that wants the first failure scans its
// slots by index.
//
// False when a call threw; the other calls of that thread are then skipped and
// the slots are not to be trusted.
template <typename Body>
[[nodiscard]]
bool fork_join_for(size_t count, Body const& body) {
    if (count == 0) {
        return true;
    }
    size_t const workers = fork_join_workers(count);
    // One flag per thread, written only by it and read after the join.
    std::vector<char> threw(workers, 0);
    auto const run = [&](size_t worker) {
        try {
            for (size_t i = worker; i < count; i += workers) {
                body(i);
            }
        } catch (...) {
            // A thread must not let an exception escape (std::terminate).
            threw[worker] = 1;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (size_t w = 1; w < workers; ++w) {
        try {
            threads.emplace_back(run, w);
        } catch (...) {
            // No thread to start: this one takes that share too.
            run(w);
        }
    }
    run(0);
    for (auto& t : threads) {
        t.join();
    }
    return std::ranges::none_of(threw, [](char t) { return t != 0; });
}

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
