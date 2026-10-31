// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_DAA_ASERTI3_2D_HPP_
#define KTH_DOMAIN_CHAIN_DAA_ASERTI3_2D_HPP_
#if defined(KTH_CURRENCY_BCH)

#include <cstdint>
#include <utility>

#include <kth/infrastructure/utility/operators.hpp>

namespace kth::domain::chain::daa {

namespace {

// precondition: TODO
// postcondition: exponent == ((return.first + 16) * 65536) + return.second
inline
std::pair<int64_t, uint16_t> shifts_frac_ticks(int64_t time_diff, int64_t tick_term, uint32_t half_life) noexcept {
    int64_t const exponent = ((time_diff - tick_term) * 65536) / half_life;
    return {sar(exponent, 16) - 16, uint16_t(exponent)};
}

inline
std::pair<int64_t, uint16_t> shifts_frac(int64_t time_diff, int64_t height_diff, uint32_t target_spacing_seconds, uint32_t half_life) noexcept {
    return shifts_frac_ticks(time_diff, int64_t(target_spacing_seconds) * height_diff, half_life);
}

inline
std::pair<int64_t, uint32_t> shifts_factor_from(std::pair<int64_t, uint16_t> const& sf) noexcept {
    auto const [shifts, frac] = sf;

    // multiply target by 65536 * 2^(fractional part)
    // 2^x ~= (1 + 0.695502049*x + 0.2262698*x**2 + 0.0782318*x**3) for 0 <= x < 1
    // Error versus actual 2^x is less than 0.013%.
    uint32_t const factor = 65536 + ((
        + 195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
        ) >> 48);

    return {shifts, factor};
}

inline
std::pair<int64_t, uint32_t> shifts_factor(int64_t time_diff, int64_t height_diff, uint32_t target_spacing_seconds, uint32_t half_life) noexcept {
    return shifts_factor_from(shifts_frac(time_diff, height_diff, target_spacing_seconds, half_life));
}

inline
uint256_t shift_2way_safe(uint256_t x, int64_t n, uint256_t const& def) noexcept {
    if (n <= 0) {
        x >>= -n;
        // 0 is not a valid target, but 1 is.
        if (x == 0) return uint256_t(1);
        if (x > def) return def;
        return x;
    }

    // Predetect overflow that would silently discard high bits.
    if (msb(x) + n > 255) return def;
    x <<= n;
    if (x > def) return def;
    return x;
}

} // namespace

// Tick form of ASERT (CHIP-2025-03).
//   next = anchor * 2^((time_diff - tick_term) / half_life)
//          * anchor_interval / next_interval
// with tick_term = T(prev) - T(anchor) + anchor_interval. The interval ratio is
// a whole number k = anchor_interval / next_interval (every conforming interval
// divides 600) and is applied after the shift and before the final clamp, with
// the multiply guarded so it cannot wrap: above pow_limit / k the product
// exceeds pow_limit anyway.
inline
uint256_t aserti3_2d_ticks(uint256_t const& anchor_target,
                           int64_t time_diff,
                           int64_t tick_term,
                           uint64_t anchor_interval,
                           uint64_t next_interval,
                           uint256_t const& pow_limit,
                           uint32_t half_life) noexcept {
    auto const [shifts, factor] = shifts_factor_from(shifts_frac_ticks(time_diff, tick_term, half_life));
    uint256_t next = anchor_target * factor;

    // Unlike shift_2way_safe, no floor to 1 here: the ratio multiply below must
    // see the exact shifted value, and a zero stays zero (then floors to 1).
    if (shifts <= 0) {
        next >>= -shifts;
    } else if (msb(next) + shifts > 255) {
        next = pow_limit;
    } else {
        next <<= shifts;
    }

    if (next_interval != anchor_interval && next_interval != 0 && anchor_interval % next_interval == 0) {
        uint64_t const k = anchor_interval / next_interval;
        if (next > pow_limit / uint256_t(k)) {
            next = pow_limit;
        } else {
            next *= uint256_t(k);
        }
    }

    if (next == 0) return uint256_t(1);
    if (next > pow_limit) return pow_limit;
    return next;
}

inline
uint256_t aserti3_2d(uint256_t const& anchor_target,
                     uint32_t target_spacing_seconds,
                     int64_t time_diff,
                     int64_t height_diff,
                     uint256_t const& pow_limit,
                     uint32_t half_life) noexcept {

    // precondition: TODO
    // postcondition: TODO

    auto const [shifts, factor] = shifts_factor(time_diff, height_diff, target_spacing_seconds, half_life);
    return shift_2way_safe(anchor_target * factor, shifts, pow_limit);
}

} // namespace kth::domain::chain::daa

#endif // defined(KTH_CURRENCY_BCH)
#endif // KTH_DOMAIN_CHAIN_DAA_ASERTI3_2D_HPP_
