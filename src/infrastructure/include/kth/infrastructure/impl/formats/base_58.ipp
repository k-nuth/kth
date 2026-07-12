// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_58_IPP
#define KTH_INFRASTUCTURE_BASE_58_IPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Inverse of `base58_alphabet`: ASCII byte → digit value (0..57), or
// `0xff` for any non-base58 character. Consumed by both the .ipp
// template and the .cpp span/data_chunk overloads.
inline constexpr auto base58_decode_table = []() consteval {
    std::array<uint8_t, 256> table{};
    for (auto& b : table) b = 0xff;
    for (size_t i = 0; i < base58_alphabet.size(); ++i) {
        table[static_cast<uint8_t>(base58_alphabet[i])] = static_cast<uint8_t>(i);
    }
    return table;
}();

template <size_t Size>
std::expected<byte_array<Size>, base58_errc> decode_base58(std::string_view in) {
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
        if (c != base58_alphabet[0]) break;
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
        if (leading_zeros == Size) return out;
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
byte_array<base58_decoded_max_size(Size - 1)> base58_literal(char const(&string)[Size]) {
    // `Size` includes the trailing NUL; `Size - 1` is the actual char
    // count. `base58_decoded_max_size` is the ceiling of that count
    // times `log(58)/log(256)`, so any literal that decodes to
    // exactly that many bytes lands here successfully; any that
    // decodes to fewer trips the assert.
    auto const decoded = decode_base58<base58_decoded_max_size(Size - 1)>(
        std::string_view{string, Size - 1});
    KTH_ASSERT(decoded);
    return *decoded;
}

} // namespace kth

#endif
