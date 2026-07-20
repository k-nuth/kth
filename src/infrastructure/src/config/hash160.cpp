// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/hash160.hpp>

#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

// static
std::expected<hash160, kth::code> hash160::parse_from(std::string_view hexcode) {
    auto decoded = decode_base16<short_hash_size>(hexcode);
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }
    return hash160{*decoded};
}

std::string hash160::to_string() const {
    return encode_base16(value_);
}

} // namespace kth::infrastructure::config
