// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_64_IPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_64_IPP

#include <algorithm>
#include <array>

namespace kth::detail {

inline constexpr char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr auto base64_digits = [] {
    std::array<uint8_t, 256> out{};
    out.fill(255);
    for (size_t i = 0; i < 64; ++i)
        out[uint8_t(base64_alphabet[i])] = uint8_t(i);
    return out;
}();

constexpr bool base64_whitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

constexpr size_t base64_capacity(size_t length) noexcept {
    return (length / 4) * 3 + (length % 4) * 3 / 4;
}
} // namespace kth::detail

namespace kth::detail {

// precondition: output.size() is the Base64 encoding size of input.
constexpr void base64_encode_into(byte_span input, std::span<char> out) noexcept {
    size_t at = 0;
    for (size_t i = 0; i < input.size(); i += 3) {
        auto const remaining = input.size() - i;
        auto const value = (uint32_t(input[i]) << 16) | (remaining > 1 ? uint32_t(input[i + 1]) << 8 : 0) |
                           (remaining > 2 ? uint32_t(input[i + 2]) : 0);
        out[at++] = detail::base64_alphabet[(value >> 18) & 63];
        out[at++] = detail::base64_alphabet[(value >> 12) & 63];
        out[at++] = remaining > 1 ? detail::base64_alphabet[(value >> 6) & 63] : '=';
        out[at++] = remaining > 2 ? detail::base64_alphabet[value & 63] : '=';
    }
}

constexpr std::string encode_base64_scalar(byte_span input) {
    std::string out((input.size() / 3 + size_t(input.size() % 3 != 0)) * 4, '=');
    detail::base64_encode_into(input, out);
    return out;
}

constexpr std::expected<size_t, base64_errc> decode_base64_scalar(
    std::string_view input, std::span<uint8_t> output) noexcept {
    size_t written = 0;
    unsigned count = 0, padding = 0;
    uint32_t value = 0;
    for (auto c : input) {
        if (detail::base64_whitespace(c))
            continue;
        if (c == '=') {
            if (++padding > 2)
                return std::unexpected(base64_errc::invalid_padding);
            continue;
        }
        auto const digit = detail::base64_digits[uint8_t(c)];
        if (padding != 0)
            return std::unexpected(base64_errc::invalid_padding);
        if (digit == 255)
            return std::unexpected(base64_errc::invalid_character);
        value = (value << 6) | digit;
        if (++count == 4) {
            if (output.size() - written < 3)
                return std::unexpected(base64_errc::output_too_small);
            output[written++] = uint8_t(value >> 16);
            output[written++] = uint8_t(value >> 8);
            output[written++] = uint8_t(value);
            count = 0;
            value = 0;
        }
    }
    if (count == 1)
        return std::unexpected(base64_errc::invalid_length);
    if (padding != 0 && (count == 0 || padding + count != 4))
        return std::unexpected(base64_errc::invalid_padding);
    if (count >= 2) {
        if (output.size() - written < count - 1)
            return std::unexpected(base64_errc::output_too_small);
        value <<= (4 - count) * 6;
        output[written++] = uint8_t(value >> 16);
        if (count == 3)
            output[written++] = uint8_t(value >> 8);
    }
    return written;
}

constexpr std::expected<data_chunk, base64_errc> decode_base64_scalar(std::string_view input) {
    data_chunk out(detail::base64_capacity(input.size()));
    auto const result = decode_base64_scalar(input, out);
    if (!result)
        return std::unexpected(result.error());
    out.erase(out.begin() + *result, out.end());
    return out;
}

} // namespace kth::detail

namespace kth {

namespace detail {

KI_API std::string encode_base64_runtime(byte_span input);
KI_API std::expected<size_t, base64_errc> decode_base64_runtime(
    std::string_view input, std::span<uint8_t> output) noexcept;
KI_API std::expected<data_chunk, base64_errc> decode_base64_runtime(std::string_view input);
} // namespace detail

constexpr std::string encode_base64(byte_span input) {
    if consteval {
        return detail::encode_base64_scalar(input);
    } else {
        return detail::encode_base64_runtime(input);
    }
}

constexpr std::expected<size_t, base64_errc> decode_base64(std::string_view input, std::span<uint8_t> output) noexcept {
    if consteval {
        return detail::decode_base64_scalar(input, output);
    } else {
        return detail::decode_base64_runtime(input, output);
    }
}

constexpr std::expected<data_chunk, base64_errc> decode_base64(std::string_view input) {
    if consteval {
        return detail::decode_base64_scalar(input);
    } else {
        return detail::decode_base64_runtime(input);
    }
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base64_errc> decode_base64(std::string_view input) noexcept {
    byte_array<Size> output{};
    auto const count = decode_base64(input, output);
    if (!count) {
        if (count.error() == base64_errc::output_too_small)
            return std::unexpected(base64_errc::size_mismatch);
        return std::unexpected(count.error());
    }
    if (*count != Size)
        return std::unexpected(base64_errc::size_mismatch);
    return output;
}

template <fixed_string Text>
concept base64_literal = [] {
    return decode_base64({Text.content, Text.size()}).has_value();
}();

template <fixed_string Text>
    requires base64_literal<Text>
[[nodiscard]] consteval auto operator""_base64() noexcept {
    constexpr size_t size = [] {
        return decode_base64({Text.content, Text.size()})->size();
    }();
    return *decode_base64<size>({Text.content, Text.size()});
}
} // namespace kth

#endif
