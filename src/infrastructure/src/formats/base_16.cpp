// Copyright (c) 2016-2025 Knuth Project developers.
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

// Fast hex decode lookup table (256 entries, invalid chars = 255)
static constexpr std::array<uint8_t, 256> hex_decode_table = []() consteval {
    std::array<uint8_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = 255;  // Invalid by default
    }
    // Digits 0-9
    for (size_t i = 0; i < 10; ++i) {
        table['0' + i] = static_cast<uint8_t>(i);
    }
    // Uppercase A-F
    for (size_t i = 0; i < 6; ++i) {
        table['A' + i] = static_cast<uint8_t>(10 + i);
    }
    // Lowercase a-f
    for (size_t i = 0; i < 6; ++i) {
        table['a' + i] = static_cast<uint8_t>(10 + i);
    }
    return table;
}();

std::string encode_base16(data_slice data) {
    std::string result(data.size() * 2, '\0');
    auto out = result.data();
    for (auto byte : data) {
        *out++ = hex_chars[(byte >> 4) & 0x0F];
        *out++ = hex_chars[byte & 0x0F];
    }
    return result;
}

bool is_base16(char c) {
    return hex_decode_table[static_cast<uint8_t>(c)] != 255;
}

bool decode_base16(data_chunk& out, std::string_view in) {
    // This prevents a last odd character from being ignored:
    if (in.size() % 2 != 0) {
        return false;
    }

    data_chunk result(in.size() / 2);
    if ( ! decode_base16_private(result.data(), result.size(), in.data())) {
        return false;
    }

    out = result;
    return true;
}

// Bitcoin hash format (these are all reversed):
std::string encode_hash(hash_digest hash) {
    std::reverse(hash.begin(), hash.end());
    return encode_base16(hash);
}

bool decode_hash(hash_digest& out, std::string_view in) {
    if (in.size() != 2 * hash_size) {
        return false;
    }

    hash_digest result;
    if ( ! decode_base16_private(result.data(), result.size(), in.data())) {
        return false;
    }

    // Reverse:
    std::reverse_copy(result.begin(), result.end(), out.begin());
    return true;
}

hash_digest hash_literal(char const (&string)[2 * hash_size + 1]) {
    hash_digest out;
    DEBUG_ONLY(auto const success =) decode_base16_private(out.data(), out.size(), string);
    KTH_ASSERT(success);
    std::reverse(out.begin(), out.end());
    return out;
}

// For support of template implementation only, do not call directly.
bool decode_base16_private(uint8_t* out, size_t out_size, char const* in) {
    for (size_t i = 0; i < out_size; ++i) {
        auto const high = hex_decode_table[static_cast<uint8_t>(in[0])];
        auto const low = hex_decode_table[static_cast<uint8_t>(in[1])];

        if (high == 255 || low == 255) {
            return false;
        }

        out[i] = (high << 4) | low;
        in += 2;
    }
    return true;
}

} // namespace kth
