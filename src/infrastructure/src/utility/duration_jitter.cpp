// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/duration_jitter.hpp>

#include <chrono>
#include <cstdint>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth {

using namespace std::chrono;

// Randomly select a time duration in the range:
// [(expiration - expiration / ratio) .. expiration]
// Not fully testable due to lack of random engine injection.
asio::duration jitter_duration(asio::duration const& expiration, uint8_t ratio) {
    if (ratio == 0) {
        return expiration;
    }

    // Uses milliseconds level resolution.
    auto const max_expire = duration_cast<milliseconds>(expiration).count();

    // [10 secs, 4] => 10000 / 4 => 2500
    auto const limit = max_expire / ratio;

    if (limit == 0) {
        return expiration;
    }

    // [0..2^64) % 2500 => [0..2500]
    auto const random_offset = int(pseudo_random::generate<uint64_t>(0, limit));

    // (10000 - [0..2500]) => [7500..10000]
    auto const expires = max_expire - random_offset;

    // [7.5..10] second duration.
    return milliseconds(expires);
}

} // namespace kth
