// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DURATION_JITTER_HPP
#define KTH_INFRASTRUCTURE_DURATION_JITTER_HPP

#include <cstdint>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/asio.hpp>

namespace kth {

/**
 * Randomly shorten a time duration by up to `1/ratio` of its length.
 *
 * The returned duration is uniformly sampled from the range
 *   [expiration - expiration / ratio, expiration].
 *
 * Intended for jittering network timeouts and reconnect backoffs so
 * peers do not synchronize their retries. This is NOT a security-grade
 * randomization primitive: it draws from the process-wide thread-local
 * Mersenne Twister and MUST NOT be used to derive nonces, keys, or any
 * value whose secrecy or unpredictability matters.
 *
 * A `ratio` of 0 disables jitter and returns `expiration` unchanged.
 *
 * @param[in]  expiration  The upper bound (and unjittered value).
 * @param[in]  ratio       Inverse of the maximum jitter fraction.
 * @return                 The jittered duration.
 */
[[nodiscard]]
KI_API
asio::duration jitter_duration(asio::duration const& expiration, uint8_t ratio);

} // namespace kth

#endif // KTH_INFRASTRUCTURE_DURATION_JITTER_HPP
