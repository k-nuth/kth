// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_16.hpp>

#include <algorithm>
#include <array>

#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Optimized lookup table for hex encoding
static constexpr std::array<char, 16> hex_chars = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

std::string encode_base16(byte_span data) {
    std::string result(data.size() * 2, '\0');
    auto out = result.data();
    for (auto byte : data) {
        *out++ = hex_chars[(byte >> 4) & 0x0F];
        *out++ = hex_chars[byte & 0x0F];
    }
    return result;
}

bool is_base16(char c) {
    return hex_decode_table[uint8_t(c)] != 255;
}

// Bitcoin hash format (these are all reversed):
std::string encode_hash(hash_digest hash) {
    std::reverse(hash.begin(), hash.end());
    return encode_base16(hash);
}

std::expected<hash_digest, base16_errc> decode_hash(std::string_view in) {
    if (in.size() != 2 * hash_size) {
        return std::unexpected(base16_errc::odd_length);
    }

    hash_digest result;
    if ( ! detail::decode_base16(result.data(), result.size(), in.data())) {
        return std::unexpected(base16_errc::invalid_character);
    }

    // Reverse:
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace kth
