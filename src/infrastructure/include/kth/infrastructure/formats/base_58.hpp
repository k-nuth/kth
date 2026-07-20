// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_58_HPP
#define KTH_INFRASTUCTURE_BASE_58_HPP

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

enum class base58_errc {
    invalid_character,
    size_mismatch
};

// Bitcoin base58 alphabet. `[0]` is the "zero digit" ('1') used by
// leading-zero counting in decode; the template `decode_base58<Size>`
// in `base_58.ipp` also uses this via `find()`.
inline constexpr std::string_view base58_alphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

KI_API bool is_base58(char ch);
KI_API bool is_base58(std::string_view text);

/**
 * Encode data as base58.
 * @return the base58 encoded string.
 */
KI_API std::string encode_base58(byte_span unencoded);

/**
 * Decode base58 into a caller-provided buffer.
 * Zero allocation.
 * @return the number of bytes written, or an error code.
 *   `invalid_character` — input contains a non-base58 character.
 *   `size_mismatch`     — decoded output would be larger than `out`.
 * @note On error `out` may be partially mutated with intermediate
 * base256 conversion state. Callers must NOT read `out` after a
 * non-success return; treat the buffer as clobbered.
 */
[[nodiscard]]
KI_API std::expected<size_t, base58_errc>
decode_base58(std::string_view in, std::span<uint8_t> out);

/**
 * Decode base58 into a freshly-allocated `data_chunk`.
 * @return the decoded bytes, or an error code.
 */
[[nodiscard]]
KI_API std::expected<data_chunk, base58_errc> decode_base58(std::string_view in);

/**
 * Decode base58 into a fixed-size `byte_array<Size>`.
 * Zero allocation.
 * @return the decoded array, or `size_mismatch` if the input does
 * not produce exactly `Size` bytes.
 */
template <size_t Size>
[[nodiscard]]
std::expected<byte_array<Size>, base58_errc> decode_base58(std::string_view in);

// Compile-time upper bound on the number of bytes decoded from `chars`
// base58 characters. `log(58)/log(256) ≈ 0.732487`, so we take the
// ceiling of `chars * 733 / 1000`. Kept explicit (rather than a raw
// `chars * 733 / 1000 + 1`) so the formula reads as what it is —
// integer-math ceiling of `chars * 733 / 1000`.
constexpr size_t base58_decoded_max_size(size_t chars) noexcept {
    return (chars * 733 + 999) / 1000;
}

/**
 * Decode a compile-time base58 string literal to a data array.
 * Delegates to `decode_base58<base58_decoded_max_size(Size - 1)>` and
 * asserts the result on error — intended for known-good literals in
 * tests where the caller expects the input to decode to exactly the
 * upper-bound number of bytes.
 */
template <size_t Size>
byte_array<base58_decoded_max_size(Size - 1)> base58_literal(char const(&string)[Size]);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_58.ipp>

#endif

