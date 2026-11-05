// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This implementation is based on:
//  --------------------------------------------------------------------------
//  Reference implementation for rfc.zeromq.org/spec:32/Z85
//
//  This implementation provides a Z85 codec as an easy-to-reuse C class
//  designed to be easy to port into other languages.

//  --------------------------------------------------------------------------
//  Copyright (c) 2010-2013 iMatix Corporation and Contributors
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  --------------------------------------------------------------------------

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_85_IPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_85_IPP

#include <algorithm>
#include <array>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

namespace detail {

// Maps binary to base 85.
inline constexpr char base85_encoder[85 + 1] = {"0123456789"
                                                "abcdefghij"
                                                "klmnopqrst"
                                                "uvwxyzABCD"
                                                "EFGHIJKLMN"
                                                "OPQRSTUVWX"
                                                "YZ.-:+=^!/"
                                                "*?&<>()[]{"
                                                "}@%$#"};

// Maps base 85 to binary.
inline constexpr uint8_t base85_decoder[96] = {0x00, 0x44, 0x00, 0x54, 0x53, 0x52, 0x48, 0x00, 0x4B, 0x4C, 0x46, 0x41,
    0x00, 0x3F, 0x3E, 0x45, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x40, 0x00, 0x49, 0x42, 0x4A,
    0x47, 0x51, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x4D, 0x00, 0x4E, 0x43, 0x00, 0x00, 0x0A, 0x0B, 0x0C, 0x0D,
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x4F, 0x00, 0x50, 0x00, 0x00};

// Accepts only byte arrays bounded to 4 bytes.
constexpr std::expected<std::string, base85_errc> encode_base85_scalar(byte_span in) {
    size_t const size = in.size();
    if (size % 4 != 0) {
        return std::unexpected(base85_errc::invalid_length);
    }

    size_t const encoded_size = size * 5 / 4;
    std::string encoded;
    encoded.reserve(encoded_size);
    size_t byte_index = 0;
    uint32_t accumulator = 0;

    for (uint8_t const unencoded_byte : in) {
        accumulator = accumulator * 256 + unencoded_byte;
        if (++byte_index % 4 == 0) {
            for (uint32_t divise = 85 * 85 * 85 * 85; divise > 0; divise /= 85) {
                encoded.push_back(base85_encoder[accumulator / divise % 85]);
            }
            accumulator = 0;
        }
    }

    KTH_ASSERT(encoded.size() == encoded_size);
    return encoded;
}

// Accepts only strings bounded to 5 characters.
// Span-based primary — zero allocation.
constexpr std::expected<size_t, base85_errc> decode_base85_scalar(
    std::string_view in, std::span<uint8_t> out) noexcept {
    size_t const length = in.size();
    if (length % 5 != 0) {
        return std::unexpected(base85_errc::invalid_length);
    }

    size_t const decoded_size = length * 4 / 5;
    if (out.size() < decoded_size) {
        return std::unexpected(base85_errc::invalid_length);
    }

    size_t write_index = 0;
    size_t char_index = 0;
    uint32_t accumulator = 0;

    for (uint8_t const encoded_character : in) {
        auto const position = encoded_character - 32;
        // `decoder` has 96 entries (indices 0..95). `position == 96`
        // (input byte 128) previously slipped through and read OOB.
        if (position < 0 || position >= 96) {
            return std::unexpected(base85_errc::invalid_character);
        }

        accumulator = accumulator * 85 + base85_decoder[position];
        if (++char_index % 5 == 0) {
            for (uint32_t divise = 256 * 256 * 256; divise > 0; divise /= 256) {
                out[write_index++] = accumulator / divise % 256;
            }
            accumulator = 0;
        }
    }

    KTH_ASSERT(write_index == decoded_size);
    return decoded_size;
}

// Allocating overload — delegates to the span form.
constexpr std::expected<data_chunk, base85_errc> decode_base85_scalar(std::string_view in) {
    size_t const length = in.size();
    if (length % 5 != 0) {
        return std::unexpected(base85_errc::invalid_length);
    }
    data_chunk out(length * 4 / 5);
    auto const written = decode_base85_scalar(in, out);
    if (!written) {
        return std::unexpected(written.error());
    }
    return out;
}

KI_API std::expected<std::string, base85_errc> encode_base85_runtime(byte_span input);
KI_API std::expected<size_t, base85_errc> decode_base85_runtime(
    std::string_view input, std::span<uint8_t> output) noexcept;
KI_API std::expected<data_chunk, base85_errc> decode_base85_runtime(std::string_view input);

} // namespace detail

constexpr std::expected<std::string, base85_errc> encode_base85(byte_span input) {
    if consteval {
        return detail::encode_base85_scalar(input);
    } else {
        return detail::encode_base85_runtime(input);
    }
}

constexpr std::expected<size_t, base85_errc> decode_base85(std::string_view input, std::span<uint8_t> output) noexcept {
    if consteval {
        return detail::decode_base85_scalar(input, output);
    } else {
        return detail::decode_base85_runtime(input, output);
    }
}

constexpr std::expected<data_chunk, base85_errc> decode_base85(std::string_view input) {
    if consteval {
        return detail::decode_base85_scalar(input);
    } else {
        return detail::decode_base85_runtime(input);
    }
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base85_errc> decode_base85(std::string_view in) noexcept {
    // Every 5 characters produce 4 bytes; refuse straight away if the
    // requested output size can't come from ANY valid input length.
    if (in.size() * 4 / 5 != Size) {
        return std::unexpected(base85_errc::invalid_length);
    }
    byte_array<Size> out{};
    auto const written = decode_base85(in, out);
    if (!written) {
        return std::unexpected(written.error());
    }
    // `written` must equal `Size` on success (the length guard above
    // makes that a compile-time-derived invariant), but assert to
    // catch any future drift in the span-form impl.
    if (*written != Size) {
        return std::unexpected(base85_errc::invalid_length);
    }
    return out;
}

namespace detail {

template <fixed_string Text>
concept valid_base85_literal = [] {
    return decode_base85({Text.content, Text.size()}).has_value();
}();

} // namespace detail

/// Decode a checked literal to an exact-size byte array using decode_base85's validation rules.
template <fixed_string Text>
    requires detail::valid_base85_literal<Text>
[[nodiscard]] consteval auto operator""_base85() noexcept {
    constexpr size_t size = Text.size() / 5 * 4;
    return *decode_base85<size>({Text.content, Text.size()});
}

} // namespace kth

#endif
