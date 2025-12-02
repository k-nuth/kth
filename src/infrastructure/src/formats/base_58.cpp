// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_58.hpp>

#include <boost/algorithm/string.hpp>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

std::string const base58_chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool is_base58(char ch) {
    // This works because the base58 characters happen to be in sorted order
    return std::binary_search(base58_chars.begin(), base58_chars.end(), ch);
}

bool is_base58(std::string_view text) {
    auto const test = [](char ch) {
        return is_base58(ch);
    };

    return std::all_of(text.begin(), text.end(), test);
}

template <typename Data>
auto search_first_nonzero(const Data& data) -> decltype(data.cbegin()) {
    auto first_nonzero = data.cbegin();
    while (first_nonzero != data.end() && *first_nonzero == 0) {
        ++first_nonzero;
    }

    return first_nonzero;
}

size_t count_leading_zeros(byte_span unencoded) {
    // Skip and count leading '1's.
    size_t leading_zeros = 0;
    for (uint8_t const byte: unencoded) {
        if (byte != 0) {
            break;
        }

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

std::string encode_base58(byte_span unencoded) {
    size_t leading_zeros = count_leading_zeros(unencoded);

    // size = log(256) / log(58), rounded up.
    size_t const number_nonzero = unencoded.size() - leading_zeros;
    size_t const indexes_size = number_nonzero * 138 / 100 + 1;

    // Allocate enough space in big-endian base58 representation.
    data_chunk indexes(indexes_size);

    // Process the bytes.
    for (auto it = unencoded.begin() + leading_zeros; it != unencoded.end(); ++it) {
        pack_value(indexes, *it);
    }

    // Skip leading zeroes in base58 result.
    auto first_nonzero = search_first_nonzero(indexes);

    // Translate the result into a string.
    std::string encoded;
    size_t const estimated_size = leading_zeros + (indexes.end() - first_nonzero);
    encoded.reserve(estimated_size);
    encoded.assign(leading_zeros, '1');

    // Set actual main bytes.
    for (auto it = first_nonzero; it != indexes.end(); ++it) {
        size_t const index = *it;
        encoded += base58_chars[index];
    }

    return encoded;
}

size_t count_leading_zeros(std::string_view encoded) {
    // Skip and count leading '1's.
    size_t leading_zeros = 0;
    for (uint8_t const digit: encoded) {
        if (digit != base58_chars[0]) {
            break;
    }

        ++leading_zeros;
    }

    return leading_zeros;
}

void unpack_char(data_chunk& data, size_t carry) {
    for (auto it = data.rbegin(); it != data.rend(); it++) {
        carry += 58 * (*it);
        *it = carry % 256;
        carry /= 256;
    }

    KTH_ASSERT(carry == 0);
}

bool decode_base58(data_chunk& out, std::string_view in) {
    // Trim spaces and newlines around the string.
    auto const leading_zeros = count_leading_zeros(in);

    // log(58) / log(256), rounded up.
    size_t const data_size = in.size() * 733 / 1000 + 1;

    // Allocate enough space in big-endian base256 representation.
    data_chunk data(data_size);

    // Process the characters.
    for (auto it = in.begin() + leading_zeros; it != in.end(); ++it) {
        auto const carry = base58_chars.find(*it);
        if (carry == std::string::npos) {
            return false;
        }

        unpack_char(data, carry);
    }

    // Skip leading zeroes in data.
    auto first_nonzero = search_first_nonzero(data);

    // Copy result into output vector.
    data_chunk decoded;
    size_t const estimated_size = leading_zeros + (data.end() - first_nonzero);
    decoded.reserve(estimated_size);
    decoded.assign(leading_zeros, 0x00);
    decoded.insert(decoded.end(), first_nonzero, data.cend());

    out = decoded;
    return true;
}

// For support of template implementation only, do not call directly.
bool decode_base58_private(uint8_t* out, size_t out_size, char const* in) {
    data_chunk buffer;
    if ( ! decode_base58(buffer, in) || buffer.size() != out_size) {
        return false;
    }

    for (size_t i = 0; i < out_size; ++i) {
        out[i] = buffer[i];
    }

    return true;
}

} // namespace kth
