// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_58_HPP
#define KTH_INFRASTUCTURE_BASE_58_HPP

#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

KI_API bool is_base58(char ch);
KI_API bool is_base58(std::string_view text);

/**
 * Converts a base58 string to a number of bytes.
 * @return false if the input is malformed, or the wrong length.
 */
template <size_t Size>
bool decode_base58(byte_array<Size>& out, std::string_view in);

/**
 * Converts a base58 string literal to a data array.
 * This would be better as a C++11 user-defined literal,
 * but MSVC doesn't support those.
 * TODO: determine if the sizing function is always accurate.
 */
template <size_t Size>
byte_array<Size * 733 / 1000> base58_literal(char const(&string)[Size]);

/**
 * Encode data as base58.
 * @return the base58 encoded string.
 */
KI_API std::string encode_base58(data_slice unencoded);

/**
 * Attempt to decode base58 data.
 * @return false if the input contains non-base58 characters.
 */
KI_API bool decode_base58(data_chunk& out, std::string_view in);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_58.ipp>

#endif

