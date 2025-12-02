// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_16_HPP
#define KTH_INFRASTUCTURE_BASE_16_HPP

#include <expected>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth {

enum class base16_errc {
    odd_length,
    invalid_character
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
 * Convert a hex string into bytes.
 */
[[nodiscard]] constexpr std::expected<data_chunk, base16_errc> decode_base16(std::string_view in);

/**
 * Converts a hex string to a number of bytes.
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
 * @return false if the input is malformed.
 */
KI_API bool decode_hash(hash_digest& out, std::string_view in);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_16.ipp>

#endif
