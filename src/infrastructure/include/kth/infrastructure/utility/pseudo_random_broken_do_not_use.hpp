// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PSEUDO_RANDOM_BROKEN_DO_NOT_USE_HPP
#define KTH_INFRASTRUCTURE_PSEUDO_RANDOM_BROKEN_DO_NOT_USE_HPP

#include <cstdint>
#include <random>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// WARNING: everything in this file is a non-CSPRNG built on a
// thread-local Mersenne Twister seeded from a wall-clock reading. It is
// unsuitable for nonces, keys, or any value whose secrecy or
// unpredictability matters. Use it only for tests and diagnostics until
// the callers are migrated to a real CSPRNG.
struct KI_API pseudo_random_broken_do_not_use {
    template <typename Container>
    static void fill(Container& out) {
        // uniform_int_distribution is undefined for sizes < 16 bits.
        std::uniform_int_distribution<uint16_t> distribution(0, max_uint8);
        auto& twister = pseudo_random_broken_do_not_use::_get_twister_broken_do_not_use();

        auto const fill = [&distribution, &twister](uint8_t byte) {
            return distribution(twister);
        };

        std::transform(out.begin(), out.end(), out.begin(), fill);
    }

    /**
     * Shuffle a container using the default random engine.
     */
    template <typename Container>
    static void shuffle(Container& out) {
        std::shuffle(out.begin(), out.end(), _get_twister_broken_do_not_use());
    }

    /**
     * Generate a pseudo random number within the domain.
     * @return  The 64 bit number.
     */
    static uint64_t next();

    /**
     * Generate a pseudo random number within [begin, end].
     * @return  The 64 bit number.
     */
    static uint64_t next(uint64_t begin, uint64_t end);

private:
    static std::mt19937& _get_twister_broken_do_not_use();
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_PSEUDO_RANDOM_BROKEN_DO_NOT_USE_HPP
