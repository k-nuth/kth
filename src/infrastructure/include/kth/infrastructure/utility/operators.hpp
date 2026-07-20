// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_OPERATORS_HPP_
#define KTH_INFRASTRUCTURE_UTILITY_OPERATORS_HPP_

#include <limits>
#include <type_traits>

namespace kth {

template <typename I, typename I2>
// requires(Integer(I) && Integer(I2))
inline constexpr
I sar(I a, I2 n) noexcept {
#if __cplusplus >= 202002L
    // C++20 [expr.shift] p. 3.
    // Right-shift on signed integral types is an arithmetic right shift, which performs sign-extension.
    return a >> n;
#else
    //checks for implementation support of arithmetic right shifts of negative numbers.
    if constexpr (I(-1) >> 1 != I(-1)) {
        static_assert(-1 == ~0, "two's complement signed number representation assumed");
        if (a < 0 && n > 0) {
            return a >> n | ~(std::numeric_limits<std::make_unsigned_t<I>>::max() >> n);
        }
    }
    return a >> n;
#endif
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_UTILITY_OPERATORS_HPP_
