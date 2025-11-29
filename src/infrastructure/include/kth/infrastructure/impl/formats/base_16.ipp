// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_16_IPP
#define KTH_INFRASTUCTURE_BASE_16_IPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

// Simple fixed_string for compile-time string literals
template <size_t N>
struct fixed_string {
    char content[N]{};

    constexpr fixed_string(char const (&str)[N]) {
        std::copy_n(str, N, content);
    }

    [[nodiscard]] constexpr size_t size() const noexcept { return N - 1; }  // exclude null terminator
};

// Compile-time hex decode lookup table
inline constexpr std::array<uint8_t, 256> hex_decode_table = []() consteval {
    std::array<uint8_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = 255;  // Invalid by default
    }
    for (size_t i = 0; i < 10; ++i) {
        table['0' + i] = uint8_t(i);
    }
    for (size_t i = 0; i < 6; ++i) {
        table['A' + i] = uint8_t(10 + i);
        table['a' + i] = uint8_t(10 + i);
    }
    return table;
}();

namespace detail {

// constexpr hex decoding for compile-time evaluation
constexpr bool decode_base16(uint8_t* out, size_t out_size, char const* in) {
    for (size_t i = 0; i < out_size; ++i) {
        auto const high = hex_decode_table[uint8_t(in[0])];
        auto const low = hex_decode_table[uint8_t(in[1])];
        if (high == 255 || low == 255) {
            return false;
        }
        out[i] = uint8_t((high << 4) | low);
        in += 2;
    }
    return true;
}

} // namespace detail

constexpr std::expected<data_chunk, base16_errc> decode_base16(std::string_view in) {
    if (in.size() % 2 != 0) {
        return std::unexpected(base16_errc::odd_length);
    }

    data_chunk result(in.size() / 2);
    if ( ! detail::decode_base16(result.data(), result.size(), in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }

    return result;
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base16_errc> decode_base16(std::string_view in) {
    if (in.size() != 2 * Size) {
        return std::unexpected(base16_errc::odd_length);
    }

    byte_array<Size> result{};
    if ( ! detail::decode_base16(result.data(), result.size(), in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }

    return result;
}

template <fixed_string Str>
concept base16_literal =
    (Str.size() % 2 == 0) &&
    []() consteval {
        byte_array<Str.size() / 2> tmp{};
        return detail::decode_base16(tmp.data(), tmp.size(), Str.content);
    }();

// User-defined literal for base16: "deadbeef"_base16
// TODO(fernando): investigate if this can be consteval instead of constexpr
template <fixed_string Str>
    requires base16_literal<Str>
constexpr auto operator""_base16() {
    constexpr size_t out_size = Str.size() / 2;
    byte_array<out_size> result{};
    detail::decode_base16(result.data(), result.size(), Str.content);
    return result;
}

template <fixed_string Str>
concept hash_literal = base16_literal<Str> && (Str.size() == 2 * hash_size);

// User-defined literal for bitcoin hash: "000...abc"_hash
// Bitcoin hashes are displayed reversed (little-endian)
// TODO(fernando): investigate if this can be consteval instead of constexpr
template <fixed_string Str>
    requires hash_literal<Str>
constexpr hash_digest operator""_hash() {
    hash_digest result{};
    detail::decode_base16(result.data(), result.size(), Str.content);
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace kth

#endif
