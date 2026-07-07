// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_85_HPP
#define KTH_INFRASTUCTURE_BASE_85_HPP

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

enum class base85_errc {
    invalid_length,
    invalid_character
};

/**
 * Encode data as base85 (Z85).
 * @return the encoded string, or `invalid_length` if the input size
 * is not a multiple of 4.
 */
[[nodiscard]]
KI_API std::expected<std::string, base85_errc> encode_base85(byte_span in);

/**
 * Decode base85 (Z85) into a caller-provided buffer.
 * Zero allocation.
 * @return the number of bytes written, or an error code.
 *   `invalid_length`    — input size is not a multiple of 5, or `out`
 *                         is smaller than the expected `in.size() * 4 / 5`.
 *   `invalid_character` — input contains a non-base85 character.
 */
[[nodiscard]]
KI_API std::expected<size_t, base85_errc>
decode_base85(std::string_view in, std::span<uint8_t> out);

/**
 * Decode base85 (Z85) into a freshly-allocated `data_chunk`.
 * @return the decoded bytes, or an error code.
 */
[[nodiscard]]
KI_API std::expected<data_chunk, base85_errc> decode_base85(std::string_view in);

/**
 * Decode base85 (Z85) into a fixed-size `byte_array<Size>`.
 * Zero allocation — the byte_array lives on the caller's stack.
 * @return the decoded array, or an error code if the input does not
 * produce exactly `Size` bytes.
 */
template <size_t Size>
[[nodiscard]]
std::expected<byte_array<Size>, base85_errc> decode_base85(std::string_view in);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_85.ipp>

#endif
