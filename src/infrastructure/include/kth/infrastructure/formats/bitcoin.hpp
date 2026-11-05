// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_FORMATS_BITCOIN_HPP
#define KTH_INFRASTRUCTURE_FORMATS_BITCOIN_HPP

#include <array>
#include <kth/infrastructure/formats/base_10.hpp>
#include <kth/infrastructure/utility/fixed_string.hpp>

namespace kth {

inline constexpr uint8_t bitcoin_decimal_places = 8;
inline constexpr uint8_t millibitcoin_decimal_places = 5;
inline constexpr uint8_t microbitcoin_decimal_places = 2;

[[nodiscard]] constexpr std::expected<uint64_t, base10_errc> bitcoin_to_satoshi(
    std::string_view bitcoin, bool strict = true) noexcept {
    return decode_base10(bitcoin, bitcoin_decimal_places, strict);
}

[[nodiscard]] constexpr std::string satoshi_to_bitcoin(uint64_t satoshi) {
    return encode_base10(satoshi, bitcoin_decimal_places);
}

namespace detail {

template <fixed_string Text, uint8_t Places>
consteval uint64_t decimal_literal_value() {
    constexpr bool has_digit = [] {
        for (auto c : Text.content)
            if (c >= '0' && c <= '9')
                return true;
        return false;
    }();
    constexpr auto value = decode_base10({Text.content, Text.size()}, Places);
    static_assert(has_digit && value.has_value(), "invalid or unrepresentable decimal amount literal");
    return value.value_or(0);
}

template <uint8_t Places, char... Chars>
consteval uint64_t numeric_decimal_literal() {
    constexpr auto text = [] {
        std::array<char, sizeof...(Chars) + 1> out{};
        size_t i = 0;
        for (auto c : std::array{Chars...})
            if (c != '\'')
                out[i++] = c;
        return out;
    }();
    constexpr auto value = decode_base10(std::string_view(text.data()), Places);
    static_assert(value.has_value(), "invalid or unrepresentable decimal amount literal");
    return value.value_or(0);
}
} // namespace detail

/// Exact unsigned satoshis: both "1.5"_bitcoin and 1.5_bitcoin avoid floating point.
template <fixed_string Text>
[[nodiscard]] consteval uint64_t operator""_bitcoin() noexcept {
    return detail::decimal_literal_value<Text, bitcoin_decimal_places>();
}

template <char... Chars>
[[nodiscard]] consteval uint64_t operator""_bitcoin() noexcept {
    return detail::numeric_decimal_literal<bitcoin_decimal_places, Chars...>();
}

template <fixed_string Text>
[[nodiscard]] consteval uint64_t operator""_millibitcoin() noexcept {
    return detail::decimal_literal_value<Text, millibitcoin_decimal_places>();
}

template <char... Chars>
[[nodiscard]] consteval uint64_t operator""_millibitcoin() noexcept {
    return detail::numeric_decimal_literal<millibitcoin_decimal_places, Chars...>();
}

template <fixed_string Text>
[[nodiscard]] consteval uint64_t operator""_microbitcoin() noexcept {
    return detail::decimal_literal_value<Text, microbitcoin_decimal_places>();
}

template <char... Chars>
[[nodiscard]] consteval uint64_t operator""_microbitcoin() noexcept {
    return detail::numeric_decimal_literal<microbitcoin_decimal_places, Chars...>();
}

[[nodiscard]] consteval uint64_t operator""_satoshi(unsigned long long value) noexcept {
    return value;
}

template <fixed_string Text>
[[nodiscard]] consteval uint64_t operator""_satoshi() noexcept {
    return detail::decimal_literal_value<Text, 0>();
}

} // namespace kth

#endif
