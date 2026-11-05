// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_10_IPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_10_IPP

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace kth::detail {

inline constexpr auto decimal_powers = [] {
    std::array<uint64_t, 20> result{1};
    for (size_t i = 1; i < result.size(); ++i)
        result[i] = result[i - 1] * 10;
    return result;
}();

constexpr std::expected<uint64_t, base10_errc> decimal_digits(std::string_view text) noexcept {
    uint64_t value = 0;
    if (text.empty())
        return value;
    if consteval {
        bool overflow = false;
        for (auto c : text) {
            if (c < '0' || c > '9')
                return std::unexpected(base10_errc::invalid_format);
            auto const digit = uint64_t(c - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                overflow = true;
            if (!overflow)
                value = value * 10 + digit;
        }
        if (overflow)
            return std::unexpected(base10_errc::overflow);
    } else {
        auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (end != text.data() + text.size() || ec == std::errc::invalid_argument)
            return std::unexpected(base10_errc::invalid_format);
        if (ec == std::errc::result_out_of_range)
            return std::unexpected(base10_errc::overflow);
    }
    return value;
}

constexpr std::expected<uint64_t, base10_errc> decimal_scale(uint64_t value, size_t places) noexcept {
    if (value == 0)
        return value;
    if (places >= decimal_powers.size() || value > std::numeric_limits<uint64_t>::max() / decimal_powers[places])
        return std::unexpected(base10_errc::overflow);
    return value * decimal_powers[places];
}

} // namespace kth::detail

namespace kth::detail {

constexpr std::expected<uint64_t, base10_errc> decode_base10_impl(
    std::string_view amount, uint8_t decimal_places, bool strict) noexcept {
    size_t point = std::string_view::npos;
    if consteval {
        for (size_t i = 0; i < amount.size(); ++i) {
            if (amount[i] == '.') {
                point = i;
                break;
            }
        }
    } else {
        point = amount.find('.');
    }
    auto const whole = decimal_digits(amount.substr(0, point));
    auto const fraction = point == std::string_view::npos ? std::string_view{} : amount.substr(point + 1);
    auto const kept = std::min(fraction.size(), size_t(decimal_places));
    auto const fractional = decimal_digits(fraction.substr(0, kept));
    bool round = false;
    for (auto c : fraction.substr(kept)) {
        if (c < '0' || c > '9')
            return std::unexpected(base10_errc::invalid_format);
        round |= c != '0';
    }
    if ((!whole && whole.error() == base10_errc::invalid_format) ||
        (!fractional && fractional.error() == base10_errc::invalid_format))
        return std::unexpected(base10_errc::invalid_format);
    if (strict && round)
        return std::unexpected(base10_errc::excess_precision);
    if (!whole)
        return std::unexpected(whole.error());
    if (!fractional)
        return std::unexpected(fractional.error());
    auto const high = decimal_scale(*whole, decimal_places);
    auto const low = decimal_scale(*fractional, size_t(decimal_places) - kept);
    if (!high || !low || *high > std::numeric_limits<uint64_t>::max() - *low)
        return std::unexpected(base10_errc::overflow);
    auto const value = *high + *low;
    if (round && value == std::numeric_limits<uint64_t>::max())
        return std::unexpected(base10_errc::overflow);
    return value + uint64_t(round);
}

constexpr std::string encode_base10_impl(uint64_t amount, uint8_t decimal_places) {
    std::array<char, std::numeric_limits<uint64_t>::digits10 + 1> digits{};
    size_t size = 0;
    if consteval {
        do {
            digits[size++] = char('0' + amount % 10);
            amount /= 10;
        } while (amount != 0);
        std::reverse(digits.begin(), digits.begin() + size);
    } else {
        size = size_t(std::to_chars(digits.data(), digits.data() + digits.size(), amount).ptr - digits.data());
    }
    if (decimal_places == 0)
        return {digits.data(), size};
    auto const whole = size > decimal_places ? size - decimal_places : 0;
    auto fraction_end = size;
    while (fraction_end > whole && digits[fraction_end - 1] == '0')
        --fraction_end;
    auto const padding = decimal_places > size ? decimal_places - size : 0;
    auto const has_fraction = fraction_end > whole;
    std::string out;
    out.reserve(std::max(size_t(1), whole) + (has_fraction ? 1 + padding + fraction_end - whole : 0));
    if (whole != 0)
        out.append(digits.data(), whole);
    else
        out.push_back('0');
    if (has_fraction) {
        out.push_back('.');
        out.append(padding, '0');
        out.append(digits.data() + whole, fraction_end - whole);
    }
    return out;
}

} // namespace kth::detail

namespace kth::detail {

KI_API std::expected<uint64_t, base10_errc> decode_base10_runtime(
    std::string_view amount, uint8_t places, bool strict) noexcept;
KI_API std::string encode_base10_runtime(uint64_t amount, uint8_t places);
} // namespace kth::detail

namespace kth {

constexpr std::expected<uint64_t, base10_errc> decode_base10(
    std::string_view amount, uint8_t places, bool strict) noexcept {
    if consteval {
        return detail::decode_base10_impl(amount, places, strict);
    } else {
        return detail::decode_base10_runtime(amount, places, strict);
    }
}

constexpr std::string encode_base10(uint64_t amount, uint8_t places) {
    if consteval {
        return detail::encode_base10_impl(amount, places);
    } else {
        return detail::encode_base10_runtime(amount, places);
    }
}

template <fixed_string Text>
concept base10_literal = [] {
    if (Text.size() == 0)
        return false;
    for (auto c : std::string_view(Text.content, Text.size()))
        if (c < '0' || c > '9')
            return false;
    return decode_base10({Text.content, Text.size()}).has_value();
}();

/// Decode an unsigned decimal integer literal; reject invalid text and overflow.
template <fixed_string Text>
    requires base10_literal<Text>
[[nodiscard]] consteval uint64_t operator""_base10() noexcept {
    return *decode_base10({Text.content, Text.size()});
}

} // namespace kth

#endif
