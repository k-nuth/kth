// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_FORMATS_BASE_16_HPP
#define KTH_INFRASTRUCTURE_FORMATS_BASE_16_HPP

#include <cstddef>
#include <expected>
#include <kth/infrastructure/utility/fixed_string.hpp>
#include <span>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth {

enum class base16_errc {
    odd_length,
    size_mismatch,
    invalid_character
};

/// Returns true if a character is a hexadecimal digit.
/// The C standard library function `isxdigit` depends on the current locale,
/// and does not necessarily match the base16 encoding.
[[nodiscard]] constexpr bool is_base16(char c) noexcept;

/// Returns true when every character is a hexadecimal digit.
[[nodiscard]] constexpr bool is_base16(std::string_view text) noexcept;

/// Convert data into a user-readable hex string.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::string encode_base16(byte_span data);

/// Encode a 32-bit unsigned integer as exactly eight lowercase hexadecimal
/// digits, most significant digit first, including leading zeroes.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::string encode_base16(uint32_t value);

/// Decode base16 into a caller-provided buffer.
/// Zero allocation.
/// @return the number of bytes written, or an error code.
/// odd_length: odd length; size_mismatch: output span is too small;
/// invalid_character: a non-hex character.
[[nodiscard]] constexpr std::expected<size_t, base16_errc> decode_base16(
    std::string_view in, std::span<uint8_t> out) noexcept;

/// Convert a hex string into a freshly-allocated `data_chunk`.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::expected<data_chunk, base16_errc> decode_base16(std::string_view in);

/// Convert a hex string into a fixed-size `byte_array<Size>`.
/// Zero allocation.
template <size_t Size>
[[nodiscard]] constexpr std::expected<byte_array<Size>, base16_errc> decode_base16(std::string_view in) noexcept;

/// Converts a bitcoin_hash to a string.
/// The bitcoin_hash format is like base16, but with the bytes reversed.
[[nodiscard]] constexpr std::string encode_hash(hash_digest hash);

/// Encode an unsigned 256-bit integer as 64 lowercase hexadecimal digits,
/// most significant digit first, including leading zeroes.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] inline std::string encode_hash(uint256_t const& value);

/// Convert a string into a bitcoin_hash.
/// The bitcoin_hash format is like base16, but with the bytes reversed.
/// @return the decoded hash, or an error code if the input is malformed.
[[nodiscard]]
constexpr std::expected<hash_digest, base16_errc> decode_hash(std::string_view in) noexcept;

} // namespace kth

#include <kth/infrastructure/impl/formats/base_16.ipp>

#endif
