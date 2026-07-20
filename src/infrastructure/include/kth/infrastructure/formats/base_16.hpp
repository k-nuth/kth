// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_16_HPP
#define KTH_INFRASTUCTURE_BASE_16_HPP

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth {

enum class base16_errc {
    odd_length,           // input has an odd number of characters
    invalid_character,    // input contains a non-hex character
    size_mismatch,        // caller-provided buffer size doesn't match `in.size() / 2`
};

/**
 * Returns true if a character is a hexadecimal digit.
 * The C standard library function `isxdigit` depends on the current locale,
 * and does not necessarily match the base16 encoding.
 */
bool is_base16(char c);

/**
 * Convert data into a user-readable hex string.
 */
KI_API std::string encode_base16(byte_span data);

/**
 * Decode base16 into a caller-provided buffer.
 * Zero allocation.
 * @return the number of bytes written, or an error code.
 *   `odd_length`        — input length is odd.
 *   `size_mismatch`     — `in.size() / 2 != out.size()`.
 *   `invalid_character` — input contains a non-hex character.
 */
[[nodiscard]] constexpr std::expected<size_t, base16_errc>
decode_base16(std::string_view in, std::span<uint8_t> out);

/**
 * Convert a hex string into a freshly-allocated `data_chunk`.
 */
[[nodiscard]] constexpr std::expected<data_chunk, base16_errc> decode_base16(std::string_view in);

/**
 * Convert a hex string into a fixed-size `byte_array<Size>`.
 * Zero allocation.
 */
template <size_t Size>
[[nodiscard]] constexpr std::expected<byte_array<Size>, base16_errc> decode_base16(std::string_view in);

/**
 * Converts a bitcoin_hash to a string.
 * The bitcoin_hash format is like base16, but with the bytes reversed.
 */
KI_API std::string encode_hash(hash_digest hash);

/**
 * Convert a string into a bitcoin_hash.
 * The bitcoin_hash format is like base16, but with the bytes reversed.
 * @return the decoded hash, or an error code if the input is malformed.
 */
[[nodiscard]]
KI_API std::expected<hash_digest, base16_errc> decode_hash(std::string_view in);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_16.ipp>

#endif
