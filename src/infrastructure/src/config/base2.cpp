// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base2.hpp>

#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

// static
std::expected<base2, kth::code> base2::parse_from(std::string_view text) noexcept {
    auto value = binary::parse_from(text);
    if ( ! value) {
        return std::unexpected(value.error());
    }
    return base2{*value};
}

std::string base2::to_string() const {
    return value_.to_string();
}

} // namespace kth::infrastructure::config
