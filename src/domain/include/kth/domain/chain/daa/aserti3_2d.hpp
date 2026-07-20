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
std::pair<int64_t, uint16_t> shifts_frac(int64_t time_diff, int64_t height_diff, uint32_t target_spacing_seconds, uint32_t half_life) noexcept {
    int64_t const exponent = ((time_diff - target_spacing_seconds * height_diff) * 65536) / half_life;
    return {sar(exponent, 16) - 16, uint16_t(exponent)};
}

inline
std::pair<int64_t, uint32_t> shifts_factor(int64_t time_diff, int64_t height_diff, uint32_t target_spacing_seconds, uint32_t half_life) noexcept {
    auto const [shifts, frac] = shifts_frac(time_diff, height_diff, target_spacing_seconds, half_life);

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
