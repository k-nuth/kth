// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_58_IPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_58_IPP

#include <algorithm>
#include <array>
#include <cstring>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

// Inverse of `base58_alphabet`: ASCII byte → digit value (0..57), or
// `0xff` for any non-base58 character. Consumed by both the .ipp
// template and the .cpp span/data_chunk overloads.
inline constexpr auto base58_decode_table = []() consteval {
    std::array<uint8_t, 256> table{};
    for (auto &b : table)
        b = 0xff;
    for (size_t i = 0; i < base58_alphabet.size(); ++i) {
        table[static_cast<uint8_t>(base58_alphabet[i])] = static_cast<uint8_t>(i);
    }
    return table;
}();

constexpr bool is_base58(char ch) noexcept {
    return base58_decode_table[uint8_t(ch)] != 0xff;
}

constexpr bool is_base58(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return is_base58(ch);
    });
}

namespace detail {

template <typename Data>
constexpr auto base58_search_first_nonzero(Data const &data) {
    auto first_nonzero = data.cbegin();
    while (first_nonzero != data.end() && *first_nonzero == 0) {
        ++first_nonzero;
    }
    return first_nonzero;
}

constexpr size_t base58_count_leading_zero_bytes(byte_span unencoded) {
    size_t leading_zeros = 0;
    for (uint8_t const byte : unencoded) {
        if (byte != 0)
            break;
        ++leading_zeros;
    }
    return leading_zeros;
}

constexpr size_t base58_count_leading_ones(std::string_view encoded) {
    size_t leading_zeros = 0;
    for (char const c : encoded) {
        if (c != base58_alphabet[0])
            break;
        ++leading_zeros;
    }
    return leading_zeros;
}

constexpr void base58_pack_value(data_chunk &indexes, size_t carry) {
    // Apply "b58 = b58 * 256 + ch".
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
        carry += 256 * (*it);
        *it = carry % 58;
        carry /= 58;
    }
    KTH_ASSERT(carry == 0);
}

constexpr std::string encode_base58_scalar(byte_span unencoded) {
    size_t const leading_zeros = base58_count_leading_zero_bytes(unencoded);

    // size = log(256) / log(58), rounded up.
    size_t const number_nonzero = unencoded.size() - leading_zeros;
    size_t const indexes_size = number_nonzero * 138 / 100 + 1;

    data_chunk indexes(indexes_size);
    for (auto it = unencoded.begin() + leading_zeros; it != unencoded.end(); ++it) {
        base58_pack_value(indexes, *it);
    }

    auto first_nonzero = base58_search_first_nonzero(indexes);

    std::string encoded;
    size_t const estimated_size = leading_zeros + (indexes.end() - first_nonzero);
    encoded.reserve(estimated_size);
    encoded.assign(leading_zeros, base58_alphabet[0]);
    for (auto it = first_nonzero; it != indexes.end(); ++it) {
        encoded += base58_alphabet[*it];
    }
    return encoded;
}

// Zero-allocation decode into a caller-supplied buffer.
//
// The base256 conversion is done in-place on the tail of `out` (used
// as scratch), then the payload is compacted to sit right after the
// leading-zero prefix. Caller must provide at least
// `leading_zeros + (in.size() - leading_zeros) * 733/1000 + 1` bytes;
// otherwise the decode surfaces `size_mismatch`.
constexpr std::expected<size_t, base58_errc> decode_base58_scalar(
    std::string_view in, std::span<uint8_t> out) noexcept {
    size_t const leading_zeros = base58_count_leading_ones(in);
    size_t const payload_capacity = (in.size() - leading_zeros) * 733 / 1000 + 1;

    if (out.size() < leading_zeros + payload_capacity) {
        return std::unexpected(base58_errc::size_mismatch);
    }

    // Use the tail of `out` (skipping the reserved leading-zero slots)
    // as the big-endian base256 scratch. Zero it first — subsequent
    // multiply-add writes accumulate into it.
    auto scratch = out.subspan(leading_zeros, payload_capacity);
    std::fill(scratch.begin(), scratch.end(), 0);

    for (auto it = in.begin() + leading_zeros; it != in.end(); ++it) {
        auto const digit = base58_decode_table[static_cast<uint8_t>(*it)];
        if (digit == 0xff) {
            return std::unexpected(base58_errc::invalid_character);
        }
        // In-place base256 multiply-add: scratch = scratch * 58 + digit.
        size_t carry = digit;
        for (auto sit = scratch.rbegin(); sit != scratch.rend(); ++sit) {
            carry += 58 * (*sit);
            *sit = carry % 256;
            carry /= 256;
        }
        // The `payload_capacity = ceil(N * log(58)/log(256))` sizing
        // above is a strict upper bound, so `carry == 0` at loop end
        // holds for every valid input. Return an error instead of a
        // Release-silent `KTH_ASSERT` so any future sizing regression
        // surfaces as a decode failure, not a truncated result.
        if (carry != 0) {
            return std::unexpected(base58_errc::size_mismatch);
        }
    }

    // First nonzero in scratch marks the start of the actual payload.
    size_t skip = 0;
    while (skip < scratch.size() && scratch[skip] == 0) {
        ++skip;
    }
    size_t const payload_size = scratch.size() - skip;

    if (skip > 0) {
        if consteval {
            for (size_t i = 0; i < payload_size; ++i)
                out[leading_zeros + i] = scratch[skip + i];
        } else {
            std::memmove(out.data() + leading_zeros, out.data() + leading_zeros + skip, payload_size);
        }
    }
    std::fill(out.begin(), out.begin() + leading_zeros, 0);

    return leading_zeros + payload_size;
}

// Allocating overload — sizes `out` for the worst case, delegates to
// the span form, then trims. One heap allocation, no intermediate copy.
constexpr std::expected<data_chunk, base58_errc> decode_base58_scalar(std::string_view in) {
    size_t const leading_zeros = base58_count_leading_ones(in);
    size_t const payload_capacity = (in.size() - leading_zeros) * 733 / 1000 + 1;

    data_chunk out(leading_zeros + payload_capacity);
    auto const written = decode_base58_scalar(in, out);
    if (!written) {
        return std::unexpected(written.error());
    }
    out.erase(out.begin() + *written, out.end());
    return out;
}

KI_API std::string encode_base58_runtime(byte_span input);
KI_API std::expected<size_t, base58_errc> decode_base58_runtime(
    std::string_view input, std::span<uint8_t> output) noexcept;
KI_API std::expected<data_chunk, base58_errc> decode_base58_runtime(std::string_view input);

} // namespace detail

constexpr std::string encode_base58(byte_span input) {
    if consteval {
        return detail::encode_base58_scalar(input);
    } else {
        return detail::encode_base58_runtime(input);
    }
}

constexpr std::expected<size_t, base58_errc> decode_base58(std::string_view input, std::span<uint8_t> output) noexcept {
    if consteval {
        return detail::decode_base58_scalar(input, output);
    } else {
        return detail::decode_base58_runtime(input, output);
    }
}

constexpr std::expected<data_chunk, base58_errc> decode_base58(std::string_view input) {
    if consteval {
        return detail::decode_base58_scalar(input);
    } else {
        return detail::decode_base58_runtime(input);
    }
}

template <size_t Size>
constexpr std::expected<byte_array<Size>, base58_errc> decode_base58(std::string_view in) noexcept {
    // Zero-alloc, zero-copy exact-size decode.
    //
    // The algorithm unpacks base58 chars into `out` in-place (using the
    // tail as base256 scratch). No extra scratch buffer is needed
    // because we specialise for the *exact*-size case: any input that
    // would produce more than `Size` bytes surfaces as a residual carry
    // (`c != 0` at the end of an iteration); any input that produces
    // fewer than `Size` bytes leaves `scratch[0] == 0` (the payload
    // right-aligns in the fixed-size scratch, so the "extra" leading
    // slot never receives a real byte). Both are rejected as
    // `size_mismatch`.

    // Count leading '1's — each represents a leading zero byte in the output.
    size_t leading_zeros = 0;
    for (char c : in) {
        if (c != base58_alphabet[0])
            break;
        ++leading_zeros;
    }
    if (leading_zeros > Size) {
        return std::unexpected(base58_errc::size_mismatch);
    }

    // `out` starts zero — the first `leading_zeros` bytes are already
    // the correct leading zero prefix; the tail acts as scratch.
    byte_array<Size> out{};

    // All-zero-input fast path: only valid if the requested Size equals
    // the leading-zero count.
    if (in.size() == leading_zeros) {
        if (leading_zeros == Size)
            return out;
        return std::unexpected(base58_errc::size_mismatch);
    }

    std::span<uint8_t> scratch{out};
    scratch = scratch.subspan(leading_zeros);

    for (auto it = in.begin() + leading_zeros; it != in.end(); ++it) {
        auto const digit = base58_decode_table[static_cast<uint8_t>(*it)];
        if (digit == 0xff) {
            return std::unexpected(base58_errc::invalid_character);
        }
        // In-place base256 multiply-add: scratch = scratch * 58 + digit.
        size_t carry = digit;
        for (auto sit = scratch.rbegin(); sit != scratch.rend(); ++sit) {
            carry += 58 * (*sit);
            *sit = carry % 256;
            carry /= 256;
        }
        // Any residual carry means the input encodes MORE than
        // `Size - leading_zeros` bytes of payload — reject.
        if (carry != 0) {
            return std::unexpected(base58_errc::size_mismatch);
        }
    }

    // If the topmost scratch byte is still zero, the payload didn't
    // fully populate the reserved region — the input encodes FEWER
    // bytes than the requested `Size`. Reject.
    if (scratch[0] == 0) {
        return std::unexpected(base58_errc::size_mismatch);
    }

    return out;
}

template <size_t Size>
constexpr byte_array<base58_decoded_max_size(Size - 1)> base58_literal(char const (&string)[Size]) noexcept {
    // `Size` includes the trailing NUL; `Size - 1` is the actual char
    // count. `base58_decoded_max_size` is the ceiling of that count
    // times `log(58)/log(256)`, so any literal that decodes to
    // exactly that many bytes lands here successfully; any that
    // decodes to fewer trips the assert.
    auto const decoded = decode_base58<base58_decoded_max_size(Size - 1)>(std::string_view{string, Size - 1});
    KTH_ASSERT(decoded);
    return *decoded;
}

namespace detail {

template <fixed_string Text>
concept valid_base58_literal = [] {
    return decode_base58({Text.content, Text.size()}).has_value();
}();

} // namespace detail

/// Decode a checked literal to an exact-size byte array using decode_base58's validation rules.
template <fixed_string Text>
    requires detail::valid_base58_literal<Text>
[[nodiscard]] consteval auto operator""_base58() noexcept {
    constexpr size_t size = [] {
        return decode_base58({Text.content, Text.size()})->size();
    }();
    return *decode_base58<size>({Text.content, Text.size()});
}

} // namespace kth

#endif
