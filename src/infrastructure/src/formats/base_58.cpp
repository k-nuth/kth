// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_58.hpp>

#include <algorithm>
#include <cstring>

#include <boost/algorithm/string.hpp>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

bool is_base58(char ch) {
    return base58_decode_table[static_cast<uint8_t>(ch)] != 0xff;
}

bool is_base58(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
        [](char ch) { return is_base58(ch); });
}

namespace {

template <typename Data>
auto search_first_nonzero(Data const& data) {
    auto first_nonzero = data.cbegin();
    while (first_nonzero != data.end() && *first_nonzero == 0) {
        ++first_nonzero;
    }
    return first_nonzero;
}

size_t count_leading_zero_bytes(byte_span unencoded) {
    size_t leading_zeros = 0;
    for (uint8_t const byte : unencoded) {
        if (byte != 0) break;
        ++leading_zeros;
    }
    return leading_zeros;
}

size_t count_leading_ones(std::string_view encoded) {
    size_t leading_zeros = 0;
    for (char const c : encoded) {
        if (c != base58_alphabet[0]) break;
        ++leading_zeros;
    }
    return leading_zeros;
}

void pack_value(data_chunk& indexes, size_t carry) {
    // Apply "b58 = b58 * 256 + ch".
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
        carry += 256 * (*it);
        *it = carry % 58;
        carry /= 58;
    }
    KTH_ASSERT(carry == 0);
}

} // namespace

std::string encode_base58(byte_span unencoded) {
    size_t const leading_zeros = count_leading_zero_bytes(unencoded);

    // size = log(256) / log(58), rounded up.
    size_t const number_nonzero = unencoded.size() - leading_zeros;
    size_t const indexes_size = number_nonzero * 138 / 100 + 1;

    data_chunk indexes(indexes_size);
    for (auto it = unencoded.begin() + leading_zeros; it != unencoded.end(); ++it) {
        pack_value(indexes, *it);
    }

    auto first_nonzero = search_first_nonzero(indexes);

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
std::expected<size_t, base58_errc>
decode_base58(std::string_view in, std::span<uint8_t> out) {
    size_t const leading_zeros = count_leading_ones(in);
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
        std::memmove(out.data() + leading_zeros,
                     out.data() + leading_zeros + skip,
                     payload_size);
    }
    std::fill(out.begin(), out.begin() + leading_zeros, 0);

    return leading_zeros + payload_size;
}

// Allocating overload — sizes `out` for the worst case, delegates to
// the span form, then trims. One heap allocation, no intermediate copy.
std::expected<data_chunk, base58_errc> decode_base58(std::string_view in) {
    size_t const leading_zeros = count_leading_ones(in);
    size_t const payload_capacity = (in.size() - leading_zeros) * 733 / 1000 + 1;

    data_chunk out(leading_zeros + payload_capacity);
    auto const written = decode_base58(in, out);
    if ( ! written) {
        return std::unexpected(written.error());
    }
    out.resize(*written);
    return out;
}

} // namespace kth
