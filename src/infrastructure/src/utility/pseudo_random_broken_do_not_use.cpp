// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/pseudo_random_broken_do_not_use.hpp>

#include <chrono>
#include <cstdint>
#include <random>

#include <boost/thread.hpp>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

using namespace std::chrono;

// DO NOT USE srand() and rand() on MSVC as srand must be called per thread.
// Values may be truly random depending on the underlying device.

static
uint32_t get_clock_seed() {
    auto const now = high_resolution_clock::now();
    return uint32_t(now.time_since_epoch().count());
}

std::mt19937& pseudo_random_broken_do_not_use::_get_twister_broken_do_not_use() {
    // Boost.thread will clean up the thread statics using this function.
    auto const deleter = [](std::mt19937* twister) {
        delete twister;
    };

    // Maintain thread static state space.
    static boost::thread_specific_ptr<std::mt19937> twister(deleter);

    // This is thread safe because the instance is thread static.
    if (twister.get() == nullptr) {
        // Seed with high resolution clock.
        twister.reset(new std::mt19937(get_clock_seed()));
    }

    return *twister;
}

uint64_t pseudo_random_broken_do_not_use::next() {
    return next(0, max_uint64);
}

uint64_t pseudo_random_broken_do_not_use::next(uint64_t begin, uint64_t end) {
    std::uniform_int_distribution<uint64_t> distribution(begin, end);
    return distribution(_get_twister_broken_do_not_use());
}

} // namespace kth
