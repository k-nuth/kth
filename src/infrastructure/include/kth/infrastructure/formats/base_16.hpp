// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_16_HPP
#define KTH_INFRASTUCTURE_BASE_16_HPP

#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth {

/**
 * Returns true if a character is a hexadecimal digit.
 * The C standard library function `isxdigit` depends on the current locale,
 * and does not necessarily match the base16 encoding.
 */
bool is_base16(char c);

/**
 * Convert data into a user-readable hex string.
 */
KI_API std::string encode_base16(data_slice data);

/**
 * Convert a hex string into bytes.
 * @return false if the input is malformed.
 */
KI_API bool decode_base16(data_chunk& out, std::string_view in);

/**
 * Converts a hex string to a number of bytes.
 * @return false if the input is malformed, or the wrong length.
 */
template <size_t Size>
bool decode_base16(byte_array<Size>& out, std::string_view in);

/**
 * Converts a hex string literal to a data array.
 * This would be better as a C++11 user-defined literal,
 * but MSVC doesn't support those.
 */
template <size_t Size>
byte_array<(Size - 1) / 2> base16_literal(char const (&string)[Size]);

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

/**
 * Convert a hex string literal into a bitcoin_hash.
 * The bitcoin_hash format is like base16, but with the bytes reversed.
 */
KI_API hash_digest hash_literal(char const (&string)[2*hash_size + 1]);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_16.ipp>

#endif
