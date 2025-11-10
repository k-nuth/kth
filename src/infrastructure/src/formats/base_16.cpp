// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_16.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>

#include <kth/infrastructure/utility/data.hpp>

namespace kth {

std::string encode_base16(data_slice data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int val: data) {
        ss << std::setw(2) << val;
    }
    return ss.str();
}

bool is_base16(char c) {
    return
        ('0' <= c && c <= '9') ||
        ('A' <= c && c <= 'F') ||
        ('a' <= c && c <= 'f');
}

static
unsigned from_hex(char c) {
    if ('A' <= c && c <= 'F') {
        return 10 + c - 'A';
    }
    if ('a' <= c && c <= 'f') {
        return 10 + c - 'a';
    }
    return c - '0';
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
    if ( ! std::all_of(in, in + 2 * out_size, is_base16)) {
        return false;
    }

    for (size_t i = 0; i < out_size; ++i) {
        out[i] = (from_hex(in[0]) << 4) + from_hex(in[1]);
        in += 2;
    }

    return true;
}

} // namespace kth
