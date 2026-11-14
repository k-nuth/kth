// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_INFRASTRUCTURE_INTERNAL_UTF_HPP
#define KTH_INFRASTRUCTURE_INTERNAL_UTF_HPP

#include <simdutf.h>
#include <string>
#include <type_traits>
#include <kth/infrastructure/unicode/unicode.hpp>

namespace kth::detail {

// Keep typed UTF-16/32 storage: wchar_t cannot alias char16_t or char32_t.
template <typename Wide>
std::expected<std::string, unicode_error> utf_to_utf8(std::basic_string<Wide> const& input) {
    static_assert(sizeof(Wide) == 2 || sizeof(Wide) == 4);
    using unit = std::conditional_t<sizeof(Wide) == 2, char16_t, char32_t>;
    if constexpr (!std::is_same_v<Wide, unit>) {
        return utf_to_utf8(std::basic_string<unit>(input.begin(), input.end()));
    } else {
        std::string output;
        if (input.empty()) return output;
        constexpr size_t maximum_bytes = sizeof(Wide) == 2 ? 3 : 4;
        if (input.size() > output.max_size() / maximum_bytes)
            return std::unexpected(unicode_error::input_too_large);
        simdutf::result converted{simdutf::SUCCESS, 0};
        output.resize_and_overwrite(input.size() * maximum_bytes, [&](char* data, size_t) noexcept {
            if constexpr (sizeof(Wide) == 2)
                converted = simdutf::convert_utf16_to_utf8_with_errors(input.data(), input.size(), data);
            else
                converted = simdutf::convert_utf32_to_utf8_with_errors(input.data(), input.size(), data);
            return converted.error == simdutf::SUCCESS ? converted.count : size_t{0};
        });
        if (converted.error != simdutf::SUCCESS)
            return std::unexpected(unicode_error::invalid_encoding);
        return output;
    }
}

template <typename Wide>
std::expected<std::basic_string<Wide>, unicode_error> utf_from_utf8(std::string const& input) {
    static_assert(sizeof(Wide) == 2 || sizeof(Wide) == 4);
    using unit = std::conditional_t<sizeof(Wide) == 2, char16_t, char32_t>;
    if constexpr (!std::is_same_v<Wide, unit>) {
        auto const converted = utf_from_utf8<unit>(input);
        if (!converted) return std::unexpected(converted.error());
        return std::basic_string<Wide>(converted->begin(), converted->end());
    } else {
        std::basic_string<Wide> output;
        if (input.empty()) return output;
        if (input.size() > output.max_size())
            return std::unexpected(unicode_error::input_too_large);
        simdutf::result converted{simdutf::SUCCESS, 0};
        // One output code unit per input byte is a safe bound, even on invalid input.
        output.resize_and_overwrite(input.size(), [&](Wide* data, size_t) noexcept {
            if constexpr (sizeof(Wide) == 2)
                converted = simdutf::convert_utf8_to_utf16_with_errors(input.data(), input.size(), data);
            else
                converted = simdutf::convert_utf8_to_utf32_with_errors(input.data(), input.size(), data);
            return converted.error == simdutf::SUCCESS ? converted.count : size_t{0};
        });
        if (converted.error != simdutf::SUCCESS)
            return std::unexpected(unicode_error::invalid_encoding);
        return output;
    }
}

} // namespace kth::detail
#endif
