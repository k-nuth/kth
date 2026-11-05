// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_FIXED_STRING_HPP
#define KTH_INFRASTRUCTURE_UTILITY_FIXED_STRING_HPP

#include <algorithm>
#include <cstddef>

namespace kth {

/// Structural string value for checked C++20/23 literal templates.
template <size_t N>
struct fixed_string {
    char content[N]{};
    constexpr fixed_string(char const (&text)[N]) noexcept {
        std::copy_n(text, N, content);
    }
    [[nodiscard]] constexpr size_t size() const noexcept {
        return N - 1;
    }
};

} // namespace kth
#endif
