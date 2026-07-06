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
    data_chunk decoded;
    if ( ! decode_base85(decoded, std::string{base85}) || decoded.size() != hash_size) {
        return std::unexpected(kth::error::illegal_value);
    }
    hash_digest value;
    std::copy_n(decoded.begin(), hash_size, value.begin());
    return sodium{value};
}

std::string sodium::to_string() const {
    std::string decoded;
    // Z85 requires four-byte alignment (hash_digest is 32 → always OK).
    (void)encode_base85(decoded, value_);
    return decoded;
}

} // namespace kth::infrastructure::config
