// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/ec_private.hpp>

#include <string>
#include <string_view>

#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>

namespace kth::domain::config {

// static
expect<ec_private> ec_private::parse_from(std::string_view hexcode) {
    auto decoded = decode_base16<ec_secret_size>(std::string{hexcode});
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }
    if ( ! verify(*decoded)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ec_private{*decoded};
}

std::string ec_private::to_string() const {
    return encode_base16(value_);
}

} // namespace kth::domain::config
