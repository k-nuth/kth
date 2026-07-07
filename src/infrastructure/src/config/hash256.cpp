// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/hash256.hpp>

#include <expected>
#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

// static
std::expected<hash256, kth::code> hash256::parse_from(std::string_view hexcode) {
    hash_digest decoded;
    if ( ! decode_hash(decoded, hexcode)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return hash256{decoded};
}

std::string hash256::to_string() const {
    return encode_hash(value_);
}

} // namespace kth::infrastructure::config
