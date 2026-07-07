// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/sodium.hpp>

#include <algorithm>
#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_85.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

// static
std::expected<sodium, kth::code> sodium::parse_from(std::string_view base85) {
    auto const decoded = decode_base85<hash_size>(base85);
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }
    return sodium{*decoded};
}

std::string sodium::to_string() const {
    // Z85 requires four-byte alignment (hash_digest is 32 → always OK).
    return encode_base85(value_).value_or(std::string{});
}

} // namespace kth::infrastructure::config
