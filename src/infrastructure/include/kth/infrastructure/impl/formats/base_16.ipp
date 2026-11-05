// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_16_IPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_16_IPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth::detail {

// Compile-time hex decode lookup table
inline constexpr std::array<uint8_t, 256> hex_decode_table = []() consteval {
    std::array<uint8_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = 255; // Invalid by default
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

// constexpr hex decoding for compile-time evaluation
constexpr bool decode_base16_bytes(uint8_t *out, size_t out_size, char const *in) noexcept {
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

constexpr std::expected<size_t, base16_errc> decode_base16_scalar(
    std::string_view in, std::span<uint8_t> out) noexcept {
    if (in.size() % 2 != 0) {
        return std::unexpected(base16_errc::odd_length);
    }
    auto const size = in.size() / 2;
    if (size > out.size()) {
        return std::unexpected(base16_errc::size_mismatch);
    }
    if (!detail::decode_base16_bytes(out.data(), size, in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }
    return size;
}

constexpr std::expected<data_chunk, base16_errc> decode_base16_scalar(std::string_view in) {
    if (in.size() % 2 != 0) {
        return std::unexpected(base16_errc::odd_length);
    }

    data_chunk result(in.size() / 2);
    if (!detail::decode_base16_bytes(result.data(), result.size(), in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }

    return result;
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base16_errc> decode_base16_scalar(std::string_view in) noexcept {
    if (in.size() % 2 != 0)
        return std::unexpected(base16_errc::odd_length);
    if (in.size() / 2 != Size)
        return std::unexpected(base16_errc::size_mismatch);

    byte_array<Size> result{};
    if (!detail::decode_base16_bytes(result.data(), result.size(), in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }

    return result;
}

constexpr void encode_base16_into(byte_span input, std::span<char> output) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    size_t i = 0;
    for (auto byte : input) {
        output[i++] = digits[byte >> 4];
        output[i++] = digits[byte & 0x0f];
    }
}

constexpr std::string encode_base16_scalar(byte_span data) {
    std::string out(data.size() * 2, '\0');
    encode_base16_into(data, out);
    return out;
}
} // namespace kth::detail

namespace kth {

constexpr std::expected<hash_digest, base16_errc> decode_hash(std::string_view in) noexcept {
    auto result = decode_base16<hash_size>(in);
    if (!result)
        return result;
    std::reverse(result->begin(), result->end());
    return result;
}

constexpr std::string encode_base16(uint32_t value) {
    return encode_base16(to_big_endian(value));
}

constexpr std::string encode_hash(hash_digest hash) {
    std::reverse(hash.begin(), hash.end());
    return encode_base16(hash);
}

inline std::string encode_hash(uint256_t const& value) {
    return encode_hash(to_hash_digest(value));
}

namespace detail {

KI_API std::string encode_base16_runtime(byte_span input);
KI_API std::expected<size_t, base16_errc> decode_base16_runtime(std::string_view in, std::span<uint8_t> out) noexcept;
KI_API std::expected<data_chunk, base16_errc> decode_base16_runtime(std::string_view in);
} // namespace detail

constexpr std::string encode_base16(byte_span input) {
    if consteval {
        return detail::encode_base16_scalar(input);
    } else {
        return detail::encode_base16_runtime(input);
    }
}

constexpr std::expected<size_t, base16_errc> decode_base16(std::string_view in, std::span<uint8_t> out) noexcept {
    if consteval {
        return detail::decode_base16_scalar(in, out);
    } else {
        return detail::decode_base16_runtime(in, out);
    }
}

constexpr std::expected<data_chunk, base16_errc> decode_base16(std::string_view in) {
    if consteval {
        return detail::decode_base16_scalar(in);
    } else {
        return detail::decode_base16_runtime(in);
    }
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base16_errc> decode_base16(std::string_view in) noexcept {
    return detail::decode_base16_scalar<Size>(in);
}

constexpr bool is_base16(char c) noexcept {
    return detail::hex_decode_table[uint8_t(c)] != 255;
}

constexpr bool is_base16(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return is_base16(c);
    });
}

template <fixed_string Str>
concept base16_literal = (Str.size() % 2 == 0) && [] {
    for (size_t i = 0; i < Str.size(); ++i)
        if (!is_base16(Str.content[i]))
            return false;
    return true;
}();

// User-defined literal for base16: "deadbeef"_base16
template <fixed_string Str>
    requires base16_literal<Str>
[[nodiscard]] consteval auto operator""_base16() noexcept {
    constexpr size_t out_size = Str.size() / 2;
    byte_array<out_size> result{};
    detail::decode_base16_bytes(result.data(), result.size(), Str.content);
    return result;
}

template <fixed_string Str>
concept hash_literal = base16_literal<Str> && (Str.size() == 2 * hash_size);

// User-defined literal for bitcoin hash: "000...abc"_hash
// Bitcoin hashes are displayed reversed (little-endian)
template <fixed_string Str>
    requires hash_literal<Str>
[[nodiscard]] consteval auto operator""_hash() noexcept {
    hash_digest result{};
    detail::decode_base16_bytes(result.data(), result.size(), Str.content);
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace kth

#endif
