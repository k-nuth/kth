// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_FORMATS_BASE_64_HPP
#define KTH_INFRASTRUCTURE_FORMATS_BASE_64_HPP

#include <span>
#include <string>
#include <string_view>
#include <kth/infrastructure/define.hpp>
#include <expected>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/fixed_string.hpp>

namespace kth {

enum class base64_errc {
    invalid_character,
    invalid_padding,
    invalid_length,
    output_too_small,
    size_mismatch
};

/// Standard Base64 encoding. Compile time uses scalar code; runtime uses the
/// native SIMD backend or the WASM SIMD128 path when enabled.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::string encode_base64(byte_span input);

/// WHATWG forgiving Base64: ASCII whitespace and optional padding are accepted.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::expected<data_chunk, base64_errc> decode_base64(std::string_view input);

/// Decode forgiving Base64 into a caller-provided buffer without allocation.
/// @return the number of bytes written, or an error for invalid input or insufficient space.
/// @note On error the output may contain a partially decoded prefix.
[[nodiscard]] constexpr std::expected<size_t, base64_errc> decode_base64(
    std::string_view input, std::span<uint8_t> output) noexcept;

/// Decode Base64 into an exact-size byte array without allocation.
/// @return size_mismatch when the decoded byte count differs from Size.
template <size_t Size>
[[nodiscard]] constexpr std::expected<byte_array<Size>, base64_errc> decode_base64(std::string_view input) noexcept;

} // namespace kth

#include <kth/infrastructure/impl/formats/base_64.ipp>

#endif
